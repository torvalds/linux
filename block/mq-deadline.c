// SPDX-License-Identifier: GPL-2.0
/*
 *  MQ Deadline i/o scheduler - adaptation of the legacy deadline scheduler,
 *  for the blk-mq scheduling framework
 *
 *  Copyright (C) 2016 Jens Axboe <axboe@kernel.dk>
 */
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/blkdev.h>
#include <linux/bio.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/compiler.h>
#include <linux/rbtree.h>
#include <linux/sbitmap.h>

#include <trace/events/block.h>

#include "elevator.h"
#include "blk.h"
#include "blk-mq.h"
#include "blk-mq-debugfs.h"
#include "blk-mq-sched.h"

/*
 * See Documentation/block/deadline-iosched.rst
 */
static const int read_expire = HZ / 2;  /* max time before a read is submitted. */
static const int write_expire = 5 * HZ; /* ditto for writes, these limits are SOFT! */
/*
 * Time after which to dispatch lower priority requests even if higher
 * priority requests are pending.
 */
static const int prio_aging_expire = 10 * HZ;
static const int writes_starved = 2;    /* max times reads can starve a write */
static const int fifo_batch = 16;       /* # of sequential requests treated as one
				     by the above parameters. For throughput. */

enum dd_data_dir {
	DD_READ		= READ,
	DD_WRITE	= WRITE,
};

enum { DD_DIR_COUNT = 2 };

enum dd_prio {
	DD_RT_PRIO	= 0,
	DD_BE_PRIO	= 1,
	DD_IDLE_PRIO	= 2,
	DD_PRIO_MAX	= 2,
};

enum { DD_PRIO_COUNT = 3 };

/*
 * I/O statistics per I/O priority. It is fine if these counters overflow.
 * What matters is that these counters are at least as wide as
 * log2(max_outstanding_requests).
 */
struct io_stats_per_prio {
	uint32_t inserted;
	uint32_t merged;
	uint32_t dispatched;
	atomic_t completed;
};

/*
 * Deadline scheduler data per I/O priority (enum dd_prio). Requests are
 * present on both sort_list[] and fifo_list[].
 */
struct dd_per_prio {
	struct list_head dispatch;
	struct rb_root sort_list[DD_DIR_COUNT];
	struct list_head fifo_list[DD_DIR_COUNT];
	/* Position of the most recently dispatched request. */
	sector_t latest_pos[DD_DIR_COUNT];
	struct io_stats_per_prio stats;
};

struct deadline_data {
	/*
	 * run time data
	 */

	struct dd_per_prio per_prio[DD_PRIO_COUNT];

	/* Data direction of latest dispatched request. */
	enum dd_data_dir last_dir;
	unsigned int batching;		/* number of sequential requests made */
	unsigned int starved;		/* times reads have starved writes */

	/*
	 * settings that change how the i/o scheduler behaves
	 */
	int fifo_expire[DD_DIR_COUNT];
	int fifo_batch;
	int writes_starved;
	int front_merges;
	u32 async_depth;
	int prio_aging_expire;

	spinlock_t lock;
};

/* Maps an I/O priority class to a deadline scheduler priority. */
static const enum dd_prio ioprio_class_to_prio[] = {
	[IOPRIO_CLASS_NONE]	= DD_BE_PRIO,
	[IOPRIO_CLASS_RT]	= DD_RT_PRIO,
	[IOPRIO_CLASS_BE]	= DD_BE_PRIO,
	[IOPRIO_CLASS_IDLE]	= DD_IDLE_PRIO,
};

static inline struct rb_root *
deadline_rb_root(struct dd_per_prio *per_prio, struct request *rq)
{
	return &per_prio->sort_list[rq_data_dir(rq)];
}

/*
 * Returns the I/O priority class (IOPRIO_CLASS_*) that has been assigned to a
 * request.
 */
static u8 dd_rq_ioclass(struct request *rq)
{
	return IOPRIO_PRIO_CLASS(req_get_ioprio(rq));
}

/*
 * Return the first request for which blk_rq_pos() >= @pos.
 */
static inline struct request *deadline_from_pos(struct dd_per_prio *per_prio,
				enum dd_data_dir data_dir, sector_t pos)
{
	struct rb_node *node = per_prio->sort_list[data_dir].rb_node;
	struct request *rq, *res = NULL;

	if (!node)
		return NULL;

	rq = rb_entry_rq(node);
	while (node) {
		rq = rb_entry_rq(node);
		if (blk_rq_pos(rq) >= pos) {
			res = rq;
			node = node->rb_left;
		} else {
			node = node->rb_right;
		}
	}
	return res;
}

static void
deadline_add_rq_rb(struct dd_per_prio *per_prio, struct request *rq)
{
	struct rb_root *root = deadline_rb_root(per_prio, rq);

	elv_rb_add(root, rq);
}

static inline void
deadline_del_rq_rb(struct dd_per_prio *per_prio, struct request *rq)
{
	elv_rb_del(deadline_rb_root(per_prio, rq), rq);
}

/*
 * remove rq from rbtree and fifo.
 */
static void deadline_remove_request(struct request_queue *q,
				    struct dd_per_prio *per_prio,
				    struct request *rq)
{
	list_del_init(&rq->queuelist);

	/*
	 * We might not be on the rbtree, if we are doing an insert merge
	 */
	if (!RB_EMPTY_NODE(&rq->rb_node))
		deadline_del_rq_rb(per_prio, rq);

	elv_rqhash_del(q, rq);
	if (q->last_merge == rq)
		q->last_merge = NULL;
}

static void dd_request_merged(struct request_queue *q, struct request *req,
			      enum elv_merge type)
{
	struct deadline_data *dd = q->elevator->elevator_data;
	const u8 ioprio_class = dd_rq_ioclass(req);
	const enum dd_prio prio = ioprio_class_to_prio[ioprio_class];
	struct dd_per_prio *per_prio = &dd->per_prio[prio];

	/*
	 * if the merge was a front merge, we need to reposition request
	 */
	if (type == ELEVATOR_FRONT_MERGE) {
		elv_rb_del(deadline_rb_root(per_prio, req), req);
		deadline_add_rq_rb(per_prio, req);
	}
}

/*
 * Callback function that is invoked after @next has been merged into @req.
 */
static void dd_merged_requests(struct request_queue *q, struct request *req,
			       struct request *next)
{
	struct deadline_data *dd = q->elevator->elevator_data;
	const u8 ioprio_class = dd_rq_ioclass(next);
	const enum dd_prio prio = ioprio_class_to_prio[ioprio_class];

	lockdep_assert_held(&dd->lock);

	dd->per_prio[prio].stats.merged++;

	/*
	 * if next expires before rq, assign its expire time to rq
	 * and move into next position (next will be deleted) in fifo
	 */
	if (!list_empty(&req->queuelist) && !list_empty(&next->queuelist)) {
		if (time_before((unsigned long)next->fifo_time,
				(unsigned long)req->fifo_time)) {
			list_move(&req->queuelist, &next->queuelist);
			req->fifo_time = next->fifo_time;
		}
	}

	/*
	 * kill knowledge of next, this one is a goner
	 */
	deadline_remove_request(q, &dd->per_prio[prio], next);
}

/*
 * move an entry to dispatch queue
 */
static void
deadline_move_request(struct deadline_data *dd, struct dd_per_prio *per_prio,
		      struct request *rq)
{
	/*
	 * take it off the sort and fifo list
	 */
	deadline_remove_request(rq->q, per_prio, rq);
}

/* Number of requests queued for a given priority level. */
static u32 dd_queued(struct deadline_data *dd, enum dd_prio prio)
{
	const struct io_stats_per_prio *stats = &dd->per_prio[prio].stats;

	lockdep_assert_held(&dd->lock);

	return stats->inserted - atomic_read(&stats->completed);
}

/*
 * deadline_check_fifo returns true if and only if there are expired requests
 * in the FIFO list. Requires !list_empty(&dd->fifo_list[data_dir]).
 */
static inline bool deadline_check_fifo(struct dd_per_prio *per_prio,
				       enum dd_data_dir data_dir)
{
	struct request *rq = rq_entry_fifo(per_prio->fifo_list[data_dir].next);

	return time_is_before_eq_jiffies((unsigned long)rq->fifo_time);
}

/*
 * For the specified data direction, return the next request to
 * dispatch using arrival ordered lists.
 */
static struct request *
deadline_fifo_request(struct deadline_data *dd, struct dd_per_prio *per_prio,
		      enum dd_data_dir data_dir)
{
	if (list_empty(&per_prio->fifo_list[data_dir]))
		return NULL;

	return rq_entry_fifo(per_prio->fifo_list[data_dir].next);
}

/*
 * For the specified data direction, return the next request to
 * dispatch using sector position sorted lists.
 */
static struct request *
deadline_next_request(struct deadline_data *dd, struct dd_per_prio *per_prio,
		      enum dd_data_dir data_dir)
{
	return deadline_from_pos(per_prio, data_dir,
				 per_prio->latest_pos[data_dir]);
}

/*
 * Returns true if and only if @rq started after @latest_start where
 * @latest_start is in jiffies.
 */
static bool started_after(struct deadline_data *dd, struct request *rq,
			  unsigned long latest_start)
{
	unsigned long start_time = (unsigned long)rq->fifo_time;

	start_time -= dd->fifo_expire[rq_data_dir(rq)];

	return time_after(start_time, latest_start);
}

/*
 * deadline_dispatch_requests selects the best request according to
 * read/write expire, fifo_batch, etc and with a start time <= @latest_start.
 */
static struct request *__dd_dispatch_request(struct deadline_data *dd,
					     struct dd_per_prio *per_prio,
					     unsigned long latest_start)
{
	struct request *rq, *next_rq;
	enum dd_data_dir data_dir;
	enum dd_prio prio;
	u8 ioprio_class;

	lockdep_assert_held(&dd->lock);

	if (!list_empty(&per_prio->dispatch)) {
		rq = list_first_entry(&per_prio->dispatch, struct request,
				      queuelist);
		if (started_after(dd, rq, latest_start))
			return NULL;
		list_del_init(&rq->queuelist);
		data_dir = rq_data_dir(rq);
		goto done;
	}

	/*
	 * batches are currently reads XOR writes
	 */
	rq = deadline_next_request(dd, per_prio, dd->last_dir);
	if (rq && dd->batching < dd->fifo_batch) {
		/* we have a next request and are still entitled to batch */
		data_dir = rq_data_dir(rq);
		goto dispatch_request;
	}

	/*
	 * at this point we are not running a batch. select the appropriate
	 * data direction (read / write)
	 */

	if (!list_empty(&per_prio->fifo_list[DD_READ])) {
		BUG_ON(RB_EMPTY_ROOT(&per_prio->sort_list[DD_READ]));

		if (deadline_fifo_request(dd, per_prio, DD_WRITE) &&
		    (dd->starved++ >= dd->writes_starved))
			goto dispatch_writes;

		data_dir = DD_READ;

		goto dispatch_find_request;
	}

	/*
	 * there are either no reads or writes have been starved
	 */

	if (!list_empty(&per_prio->fifo_list[DD_WRITE])) {
dispatch_writes:
		BUG_ON(RB_EMPTY_ROOT(&per_prio->sort_list[DD_WRITE]));

		dd->starved = 0;

		data_dir = DD_WRITE;

		goto dispatch_find_request;
	}

	return NULL;

dispatch_find_request:
	/*
	 * we are not running a batch, find best request for selected data_dir
	 */
	next_rq = deadline_next_request(dd, per_prio, data_dir);
	if (deadline_check_fifo(per_prio, data_dir) || !next_rq) {
		/*
		 * A deadline has expired, the last request was in the other
		 * direction, or we have run out of higher-sectored requests.
		 * Start again from the request with the earliest expiry time.
		 */
		rq = deadline_fifo_request(dd, per_prio, data_dir);
	} else {
		/*
		 * The last req was the same dir and we have a next request in
		 * sort order. No expired requests so continue on from here.
		 */
		rq = next_rq;
	}

	if (!rq)
		return NULL;

	dd->last_dir = data_dir;
	dd->batching = 0;

dispatch_request:
	if (started_after(dd, rq, latest_start))
		return NULL;

	/*
	 * rq is the selected appropriate request.
	 */
	dd->batching++;
	deadline_move_request(dd, per_prio, rq);
done:
	ioprio_class = dd_rq_ioclass(rq);
	prio = ioprio_class_to_prio[ioprio_class];
	dd->per_prio[prio].latest_pos[data_dir] = blk_rq_pos(rq);
	dd->per_prio[prio].stats.dispatched++;
	rq->rq_flags |= RQF_STARTED;
	return rq;
}

/*
 * Check whether there are any requests with priority other than DD_RT_PRIO
 * that were inserted more than prio_aging_expire jiffies ago.
 */
static struct request *dd_dispatch_prio_aged_requests(struct deadline_data *dd,
						      unsigned long now)
{
	struct request *rq;
	enum dd_prio prio;
	int prio_cnt;

	lockdep_assert_held(&dd->lock);

	prio_cnt = !!dd_queued(dd, DD_RT_PRIO) + !!dd_queued(dd, DD_BE_PRIO) +
		   !!dd_queued(dd, DD_IDLE_PRIO);
	if (prio_cnt < 2)
		return NULL;

	for (prio = DD_BE_PRIO; prio <= DD_PRIO_MAX; prio++) {
		rq = __dd_dispatch_request(dd, &dd->per_prio[prio],
					   now - dd->prio_aging_expire);
		if (rq)
			return rq;
	}

	return NULL;
}

/*
 * Called from blk_mq_run_hw_queue() -> __blk_mq_sched_dispatch_requests().
 *
 * One confusing aspect here is that we get called for a specific
 * hardware queue, but we may return a request that is for a
 * different hardware queue. This is because mq-deadline has shared
 * state for all hardware queues, in terms of sorting, FIFOs, etc.
 */
static struct request *dd_dispatch_request(struct blk_mq_hw_ctx *hctx)
{
	struct deadline_data *dd = hctx->queue->elevator->elevator_data;
	const unsigned long now = jiffies;
	struct request *rq;
	enum dd_prio prio;

	spin_lock(&dd->lock);
	rq = dd_dispatch_prio_aged_requests(dd, now);
	if (rq)
		goto unlock;

	/*
	 * Next, dispatch requests in priority order. Ignore lower priority
	 * requests if any higher priority requests are pending.
	 */
	for (prio = 0; prio <= DD_PRIO_MAX; prio++) {
		rq = __dd_dispatch_request(dd, &dd->per_prio[prio], now);
		if (rq || dd_queued(dd, prio))
			break;
	}

unlock:
	spin_unlock(&dd->lock);

	return rq;
}

/*
 * Called by __blk_mq_alloc_request(). The shallow_depth value set by this
 * function is used by __blk_mq_get_tag().
 */
static void dd_limit_depth(blk_opf_t opf, struct blk_mq_alloc_data *data)
{
	struct deadline_data *dd = data->q->elevator->elevator_data;

	/* Do not throttle synchronous reads. */
	if (op_is_sync(opf) && !op_is_write(opf))
		return;

	/*
	 * Throttle asynchronous requests and writes such that these requests
	 * do not block the allocation of synchronous requests.
	 */
	data->shallow_depth = dd->async_depth;
}

/* Called by blk_mq_update_nr_requests(). */
static void dd_depth_updated(struct blk_mq_hw_ctx *hctx)
{
	struct request_queue *q = hctx->queue;
	struct deadline_data *dd = q->elevator->elevator_data;
	struct blk_mq_tags *tags = hctx->sched_tags;

	dd->async_depth = q->nr_requests;

	sbitmap_queue_min_shallow_depth(&tags->bitmap_tags, 1);
}

/* Called by blk_mq_init_hctx() and blk_mq_init_sched(). */
static int dd_init_hctx(struct blk_mq_hw_ctx *hctx, unsigned int hctx_idx)
{
	dd_depth_updated(hctx);
	return 0;
}

static void dd_exit_sched(struct elevator_queue *e)
{
	struct deadline_data *dd = e->elevator_data;
	enum dd_prio prio;

	for (prio = 0; prio <= DD_PRIO_MAX; prio++) {
		struct dd_per_prio *per_prio = &dd->per_prio[prio];
		const struct io_stats_per_prio *stats = &per_prio->stats;
		uint32_t queued;

		WARN_ON_ONCE(!list_empty(&per_prio->fifo_list[DD_READ]));
		WARN_ON_ONCE(!list_empty(&per_prio->fifo_list[DD_WRITE]));

		spin_lock(&dd->lock);
		queued = dd_queued(dd, prio);
		spin_unlock(&dd->lock);

		WARN_ONCE(queued != 0,
			  "statistics for priority %d: i %u m %u d %u c %u\n",
			  prio, stats->inserted, stats->merged,
			  stats->dispatched, atomic_read(&stats->completed));
	}

	kfree(dd);
}

/*
 * initialize elevator private data (deadline_data).
 */
static int dd_init_sched(struct request_queue *q, struct elevator_queue *eq)
{
	struct deadline_data *dd;
	enum dd_prio prio;

	dd = kzalloc_node(sizeof(*dd), GFP_KERNEL, q->node);
	if (!dd)
		return -ENOMEM;

	eq->elevator_data = dd;

	for (prio = 0; prio <= DD_PRIO_MAX; prio++) {
		struct dd_per_prio *per_prio = &dd->per_prio[prio];

		INIT_LIST_HEAD(&per_prio->dispatch);
		INIT_LIST_HEAD(&per_prio->fifo_list[DD_READ]);
		INIT_LIST_HEAD(&per_prio->fifo_list[DD_WRITE]);
		per_prio->sort_list[DD_READ] = RB_ROOT;
		per_prio->sort_list[DD_WRITE] = RB_ROOT;
	}
	dd->fifo_expire[DD_READ] = read_expire;
	dd->fifo_expire[DD_WRITE] = write_expire;
	dd->writes_starved = writes_starved;
	dd->front_merges = 1;
	dd->last_dir = DD_WRITE;
	dd->fifo_batch = fifo_batch;
	dd->prio_aging_expire = prio_aging_expire;
	spin_lock_init(&dd->lock);

	/* We dispatch from request queue wide instead of hw queue */
	blk_queue_flag_set(QUEUE_FLAG_SQ_SCHED, q);

	q->elevator = eq;
	return 0;
}

/*
 * Try to merge @bio into an existing request. If @bio has been merged into
 * an existing request, store the pointer to that request into *@rq.
 */
static int dd_request_merge(struct request_queue *q, struct request **rq,
			    struct bio *bio)
{
	struct deadline_data *dd = q->elevator->elevator_data;
	const u8 ioprio_class = IOPRIO_PRIO_CLASS(bio->bi_ioprio);
	const enum dd_prio prio = ioprio_class_to_prio[ioprio_class];
	struct dd_per_prio *per_prio = &dd->per_prio[prio];
	sector_t sector = bio_end_sector(bio);
	struct request *__rq;

	if (!dd->front_merges)
		return ELEVATOR_NO_MERGE;

	__rq = elv_rb_find(&per_prio->sort_list[bio_data_dir(bio)], sector);
	if (__rq) {
		BUG_ON(sector != blk_rq_pos(__rq));

		if (elv_bio_merge_ok(__rq, bio)) {
			*rq = __rq;
			if (blk_discard_mergable(__rq))
				return ELEVATOR_DISCARD_MERGE;
			return ELEVATOR_FRONT_MERGE;
		}
	}

	return ELEVATOR_NO_MERGE;
}

/*
 * Attempt to merge a bio into an existing request. This function is called
 * before @bio is associated with a request.
 */
static bool dd_bio_merge(struct request_queue *q, struct bio *bio,
		unsigned int nr_segs)
{
	struct deadline_data *dd = q->elevator->elevator_data;
	struct request *free = NULL;
	bool ret;

	spin_lock(&dd->lock);
	ret = blk_mq_sched_try_merge(q, bio, nr_segs, &free);
	spin_unlock(&dd->lock);

	if (free)
		blk_mq_free_request(free);

	return ret;
}

/*
 * add rq to rbtree and fifo
 */
static void dd_insert_request(struct blk_mq_hw_ctx *hctx, struct request *rq,
			      blk_insert_t flags, struct list_head *free)
{
	struct request_queue *q = hctx->queue;
	struct deadline_data *dd = q->elevator->elevator_data;
	const enum dd_data_dir data_dir = rq_data_dir(rq);
	u16 ioprio = req_get_ioprio(rq);
	u8 ioprio_class = IOPRIO_PRIO_CLASS(ioprio);
	struct dd_per_prio *per_prio;
	enum dd_prio prio;

	lockdep_assert_held(&dd->lock);

	prio = ioprio_class_to_prio[ioprio_class];
	per_prio = &dd->per_prio[prio];
	if (!rq->elv.priv[0])
		per_prio->stats.inserted++;
	rq->elv.priv[0] = per_prio;

	if (blk_mq_sched_try_insert_merge(q, rq, free))
		return;

	trace_block_rq_insert(rq);

	if (flags & BLK_MQ_INSERT_AT_HEAD) {
		list_add(&rq->queuelist, &per_prio->dispatch);
		rq->fifo_time = jiffies;
	} else {
		deadline_add_rq_rb(per_prio, rq);

		if (rq_mergeable(rq)) {
			elv_rqhash_add(q, rq);
			if (!q->last_merge)
				q->last_merge = rq;
		}

		/*
		 * set expire time and add to fifo list
		 */
		rq->fifo_time = jiffies + dd->fifo_expire[data_dir];
		list_add_tail(&rq->queuelist, &per_prio->fifo_list[data_dir]);
	}
}

/*
 * Called from blk_mq_insert_request() or blk_mq_dispatch_list().
 */
static void dd_insert_requests(struct blk_mq_hw_ctx *hctx,
			       struct list_head *list,
			       blk_insert_t flags)
{
	struct request_queue *q = hctx->queue;
	struct deadline_data *dd = q->elevator->elevator_data;
	LIST_HEAD(free);

	spin_lock(&dd->lock);
	while (!list_empty(list)) {
		struct request *rq;

		rq = list_first_entry(list, struct request, queuelist);
		list_del_init(&rq->queuelist);
		dd_insert_request(hctx, rq, flags, &free);
	}
	spin_unlock(&dd->lock);

	blk_mq_free_requests(&free);
}

/* Callback from inside blk_mq_rq_ctx_init(). */
static void dd_prepare_request(struct request *rq)
{
	rq->elv.priv[0] = NULL;
}

/*
 * Callback from inside blk_mq_free_request().
 */
static void dd_finish_request(struct request *rq)
{
	struct dd_per_prio *per_prio = rq->elv.priv[0];

	/*
	 * The block layer core may call dd_finish_request() without having
	 * called dd_insert_requests(). Skip requests that bypassed I/O
	 * scheduling. See also blk_mq_request_bypass_insert().
	 */
	if (per_prio)
		atomic_inc(&per_prio->stats.completed);
}

static bool dd_has_work_for_prio(struct dd_per_prio *per_prio)
{
	return !list_empty_careful(&per_prio->dispatch) ||
		!list_empty_careful(&per_prio->fifo_list[DD_READ]) ||
		!list_empty_careful(&per_prio->fifo_list[DD_WRITE]);
}

static bool dd_has_work(struct blk_mq_hw_ctx *hctx)
{
	struct deadline_data *dd = hctx->queue->elevator->elevator_data;
	enum dd_prio prio;

	for (prio = 0; prio <= DD_PRIO_MAX; prio++)
		if (dd_has_work_for_prio(&dd->per_prio[prio]))
			return true;

	return false;
}

/*
 * sysfs parts below
 */
#define SHOW_INT(__FUNC, __VAR)						\
static ssize_t __FUNC(struct elevator_queue *e, char *page)		\
{									\
	struct deadline_data *dd = e->elevator_data;			\
									\
	return sysfs_emit(page, "%d\n", __VAR);				\
}
#define SHOW_JIFFIES(__FUNC, __VAR) SHOW_INT(__FUNC, jiffies_to_msecs(__VAR))
SHOW_JIFFIES(deadline_read_expire_show, dd->fifo_expire[DD_READ]);
SHOW_JIFFIES(deadline_write_expire_show, dd->fifo_expire[DD_WRITE]);
SHOW_JIFFIES(deadline_prio_aging_expire_show, dd->prio_aging_expire);
SHOW_INT(deadline_writes_starved_show, dd->writes_starved);
SHOW_INT(deadline_front_merges_show, dd->front_merges);
SHOW_INT(deadline_async_depth_show, dd->async_depth);
SHOW_INT(deadline_fifo_batch_show, dd->fifo_batch);
#undef SHOW_INT
#undef SHOW_JIFFIES

#define STORE_FUNCTION(__FUNC, __PTR, MIN, MAX, __CONV)			\
static ssize_t __FUNC(struct elevator_queue *e, const char *page, size_t count)	\
{									\
	struct deadline_data *dd = e->elevator_data;			\
	int __data, __ret;						\
									\
	__ret = kstrtoint(page, 0, &__data);				\
	if (__ret < 0)							\
		return __ret;						\
	if (__data < (MIN))						\
		__data = (MIN);						\
	else if (__data > (MAX))					\
		__data = (MAX);						\
	*(__PTR) = __CONV(__data);					\
	return count;							\
}
#define STORE_INT(__FUNC, __PTR, MIN, MAX)				\
	STORE_FUNCTION(__FUNC, __PTR, MIN, MAX, )
#define STORE_JIFFIES(__FUNC, __PTR, MIN, MAX)				\
	STORE_FUNCTION(__FUNC, __PTR, MIN, MAX, msecs_to_jiffies)
STORE_JIFFIES(deadline_read_expire_store, &dd->fifo_expire[DD_READ], 0, INT_MAX);
STORE_JIFFIES(deadline_write_expire_store, &dd->fifo_expire[DD_WRITE], 0, INT_MAX);
STORE_JIFFIES(deadline_prio_aging_expire_store, &dd->prio_aging_expire, 0, INT_MAX);
STORE_INT(deadline_writes_starved_store, &dd->writes_starved, INT_MIN, INT_MAX);
STORE_INT(deadline_front_merges_store, &dd->front_merges, 0, 1);
STORE_INT(deadline_async_depth_store, &dd->async_depth, 1, INT_MAX);
STORE_INT(deadline_fifo_batch_store, &dd->fifo_batch, 0, INT_MAX);
#undef STORE_FUNCTION
#undef STORE_INT
#undef STORE_JIFFIES

#define DD_ATTR(name) \
	__ATTR(name, 0644, deadline_##name##_show, deadline_##name##_store)

static const struct elv_fs_entry deadline_attrs[] = {
	DD_ATTR(read_expire),
	DD_ATTR(write_expire),
	DD_ATTR(writes_starved),
	DD_ATTR(front_merges),
	DD_ATTR(async_depth),
	DD_ATTR(fifo_batch),
	DD_ATTR(prio_aging_expire),
	__ATTR_NULL
};

#ifdef CONFIG_BLK_DEBUG_FS
#define DEADLINE_DEBUGFS_DDIR_ATTRS(prio, data_dir, name)		\
static void *deadline_##name##_fifo_start(struct seq_file *m,		\
					  loff_t *pos)			\
	__acquires(&dd->lock)						\
{									\
	struct request_queue *q = m->private;				\
	struct deadline_data *dd = q->elevator->elevator_data;		\
	struct dd_per_prio *per_prio = &dd->per_prio[prio];		\
									\
	spin_lock(&dd->lock);						\
	return seq_list_start(&per_prio->fifo_list[data_dir], *pos);	\
}									\
									\
static void *deadline_##name##_fifo_next(struct seq_file *m, void *v,	\
					 loff_t *pos)			\
{									\
	struct request_queue *q = m->private;				\
	struct deadline_data *dd = q->elevator->elevator_data;		\
	struct dd_per_prio *per_prio = &dd->per_prio[prio];		\
									\
	return seq_list_next(v, &per_prio->fifo_list[data_dir], pos);	\
}									\
									\
static void deadline_##name##_fifo_stop(struct seq_file *m, void *v)	\
	__releases(&dd->lock)						\
{									\
	struct request_queue *q = m->private;				\
	struct deadline_data *dd = q->elevator->elevator_data;		\
									\
	spin_unlock(&dd->lock);						\
}									\
									\
static const struct seq_operations deadline_##name##_fifo_seq_ops = {	\
	.start	= deadline_##name##_fifo_start,				\
	.next	= deadline_##name##_fifo_next,				\
	.stop	= deadline_##name##_fifo_stop,				\
	.show	= blk_mq_debugfs_rq_show,				\
};									\
									\
static int deadline_##name##_next_rq_show(void *data,			\
					  struct seq_file *m)		\
{									\
	struct request_queue *q = data;					\
	struct deadline_data *dd = q->elevator->elevator_data;		\
	struct dd_per_prio *per_prio = &dd->per_prio[prio];		\
	struct request *rq;						\
									\
	rq = deadline_from_pos(per_prio, data_dir,			\
			       per_prio->latest_pos[data_dir]);		\
	if (rq)								\
		__blk_mq_debugfs_rq_show(m, rq);			\
	return 0;							\
}

DEADLINE_DEBUGFS_DDIR_ATTRS(DD_RT_PRIO, DD_READ, read0);
DEADLINE_DEBUGFS_DDIR_ATTRS(DD_RT_PRIO, DD_WRITE, write0);
DEADLINE_DEBUGFS_DDIR_ATTRS(DD_BE_PRIO, DD_READ, read1);
DEADLINE_DEBUGFS_DDIR_ATTRS(DD_BE_PRIO, DD_WRITE, write1);
DEADLINE_DEBUGFS_DDIR_ATTRS(DD_IDLE_PRIO, DD_READ, read2);
DEADLINE_DEBUGFS_DDIR_ATTRS(DD_IDLE_PRIO, DD_WRITE, write2);
#undef DEADLINE_DEBUGFS_DDIR_ATTRS

static int deadline_batching_show(void *data, struct seq_file *m)
{
	struct request_queue *q = data;
	struct deadline_data *dd = q->elevator->elevator_data;

	seq_printf(m, "%u\n", dd->batching);
	return 0;
}

static int deadline_starved_show(void *data, struct seq_file *m)
{
	struct request_queue *q = data;
	struct deadline_data *dd = q->elevator->elevator_data;

	seq_printf(m, "%u\n", dd->starved);
	return 0;
}

static int dd_async_depth_show(void *data, struct seq_file *m)
{
	struct request_queue *q = data;
	struct deadline_data *dd = q->elevator->elevator_data;

	seq_printf(m, "%u\n", dd->async_depth);
	return 0;
}

static int dd_queued_show(void *data, struct seq_file *m)
{
	struct request_queue *q = data;
	struct deadline_data *dd = q->elevator->elevator_data;
	u32 rt, be, idle;

	spin_lock(&dd->lock);
	rt = dd_queued(dd, DD_RT_PRIO);
	be = dd_queued(dd, DD_BE_PRIO);
	idle = dd_queued(dd, DD_IDLE_PRIO);
	spin_unlock(&dd->lock);

	seq_printf(m, "%u %u %u\n", rt, be, idle);

	return 0;
}

/* Number of requests owned by the block driver for a given priority. */
static u32 dd_owned_by_driver(struct deadline_data *dd, enum dd_prio prio)
{
	const struct io_stats_per_prio *stats = &dd->per_prio[prio].stats;

	lockdep_assert_held(&dd->lock);

	return stats->dispatched + stats->merged -
		atomic_read(&stats->completed);
}

static int dd_owned_by_driver_show(void *data, struct seq_file *m)
{
	struct request_queue *q = data;
	struct deadline_data *dd = q->elevator->elevator_data;
	u32 rt, be, idle;

	spin_lock(&dd->lock);
	rt = dd_owned_by_driver(dd, DD_RT_PRIO);
	be = dd_owned_by_driver(dd, DD_BE_PRIO);
	idle = dd_owned_by_driver(dd, DD_IDLE_PRIO);
	spin_unlock(&dd->lock);

	seq_printf(m, "%u %u %u\n", rt, be, idle);

	return 0;
}

#define DEADLINE_DISPATCH_ATTR(prio)					\
static void *deadline_dispatch##prio##_start(struct seq_file *m,	\
					     loff_t *pos)		\
	__acquires(&dd->lock)						\
{									\
	struct request_queue *q = m->private;				\
	struct deadline_data *dd = q->elevator->elevator_data;		\
	struct dd_per_prio *per_prio = &dd->per_prio[prio];		\
									\
	spin_lock(&dd->lock);						\
	return seq_list_start(&per_prio->dispatch, *pos);		\
}									\
									\
static void *deadline_dispatch##prio##_next(struct seq_file *m,		\
					    void *v, loff_t *pos)	\
{									\
	struct request_queue *q = m->private;				\
	struct deadline_data *dd = q->elevator->elevator_data;		\
	struct dd_per_prio *per_prio = &dd->per_prio[prio];		\
									\
	return seq_list_next(v, &per_prio->dispatch, pos);		\
}									\
									\
static void deadline_dispatch##prio##_stop(struct seq_file *m, void *v)	\
	__releases(&dd->lock)						\
{									\
	struct request_queue *q = m->private;				\
	struct deadline_data *dd = q->elevator->elevator_data;		\
									\
	spin_unlock(&dd->lock);						\
}									\
									\
static const struct seq_operations deadline_dispatch##prio##_seq_ops = { \
	.start	= deadline_dispatch##prio##_start,			\
	.next	= deadline_dispatch##prio##_next,			\
	.stop	= deadline_dispatch##prio##_stop,			\
	.show	= blk_mq_debugfs_rq_show,				\
}

DEADLINE_DISPATCH_ATTR(0);
DEADLINE_DISPATCH_ATTR(1);
DEADLINE_DISPATCH_ATTR(2);
#undef DEADLINE_DISPATCH_ATTR

#define DEADLINE_QUEUE_DDIR_ATTRS(name)					\
	{#name "_fifo_list", 0400,					\
			.seq_ops = &deadline_##name##_fifo_seq_ops}
#define DEADLINE_NEXT_RQ_ATTR(name)					\
	{#name "_next_rq", 0400, deadline_##name##_next_rq_show}
static const struct blk_mq_debugfs_attr deadline_queue_debugfs_attrs[] = {
	DEADLINE_QUEUE_DDIR_ATTRS(read0),
	DEADLINE_QUEUE_DDIR_ATTRS(write0),
	DEADLINE_QUEUE_DDIR_ATTRS(read1),
	DEADLINE_QUEUE_DDIR_ATTRS(write1),
	DEADLINE_QUEUE_DDIR_ATTRS(read2),
	DEADLINE_QUEUE_DDIR_ATTRS(write2),
	DEADLINE_NEXT_RQ_ATTR(read0),
	DEADLINE_NEXT_RQ_ATTR(write0),
	DEADLINE_NEXT_RQ_ATTR(read1),
	DEADLINE_NEXT_RQ_ATTR(write1),
	DEADLINE_NEXT_RQ_ATTR(read2),
	DEADLINE_NEXT_RQ_ATTR(write2),
	{"batching", 0400, deadline_batching_show},
	{"starved", 0400, deadline_starved_show},
	{"async_depth", 0400, dd_async_depth_show},
	{"dispatch0", 0400, .seq_ops = &deadline_dispatch0_seq_ops},
	{"dispatch1", 0400, .seq_ops = &deadline_dispatch1_seq_ops},
	{"dispatch2", 0400, .seq_ops = &deadline_dispatch2_seq_ops},
	{"owned_by_driver", 0400, dd_owned_by_driver_show},
	{"queued", 0400, dd_queued_show},
	{},
};
#undef DEADLINE_QUEUE_DDIR_ATTRS
#endif

static struct elevator_type mq_deadline = {
	.ops = {
		.depth_updated		= dd_depth_updated,
		.limit_depth		= dd_limit_depth,
		.insert_requests	= dd_insert_requests,
		.dispatch_request	= dd_dispatch_request,
		.prepare_request	= dd_prepare_request,
		.finish_request		= dd_finish_request,
		.next_request		= elv_rb_latter_request,
		.former_request		= elv_rb_former_request,
		.bio_merge		= dd_bio_merge,
		.request_merge		= dd_request_merge,
		.requests_merged	= dd_merged_requests,
		.request_merged		= dd_request_merged,
		.has_work		= dd_has_work,
		.init_sched		= dd_init_sched,
		.exit_sched		= dd_exit_sched,
		.init_hctx		= dd_init_hctx,
	},

#ifdef CONFIG_BLK_DEBUG_FS
	.queue_debugfs_attrs = deadline_queue_debugfs_attrs,
#endif
	.elevator_attrs = deadline_attrs,
	.elevator_name = "mq-deadline",
	.elevator_alias = "deadline",
	.elevator_owner = THIS_MODULE,
};
MODULE_ALIAS("mq-deadline-iosched");

static int __init deadline_init(void)
{
	return elv_register(&mq_deadline);
}

static void __exit deadline_exit(void)
{
	elv_unregister(&mq_deadline);
}

module_init(deadline_init);
module_exit(deadline_exit);

MODULE_AUTHOR("Jens Axboe, Damien Le Moal and Bart Van Assche");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MQ deadline IO scheduler");
