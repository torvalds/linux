/*
 *  MQ Deadline i/o scheduler - adaptation of the legacy deadline scheduler,
 *  for the blk-mq scheduling framework
 *
 *  Copyright (C) 2016 Jens Axboe <axboe@kernel.dk>
 */
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/blkdev.h>
#include <linux/blk-mq.h>
#include <linux/elevator.h>
#include <linux/bio.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/compiler.h>
#include <linux/rbtree.h>
#include <linux/sbitmap.h>

#include "blk.h"
#include "blk-mq.h"
#include "blk-mq-debugfs.h"
#include "blk-mq-tag.h"
#include "blk-mq-sched.h"

/*
 * See Documentation/block/deadline-iosched.txt
 */
static const int read_expire = HZ / 2;  /* max time before a read is submitted. */
static const int write_expire = 5 * HZ; /* ditto for writes, these limits are SOFT! */
static const int writes_starved = 2;    /* max times reads can starve a write */
static const int fifo_batch = 16;       /* # of sequential requests treated as one
				     by the above parameters. For throughput. */

struct deadline_data {
	/*
	 * run time data
	 */

	/*
	 * requests (deadline_rq s) are present on both sort_list and fifo_list
	 */
	struct rb_root sort_list[2];
	struct list_head fifo_list[2];

	/*
	 * next in sort order. read, write or both are NULL
	 */
	struct request *next_rq[2];
	unsigned int batching;		/* number of sequential requests made */
	unsigned int starved;		/* times reads have starved writes */

	/*
	 * settings that change how the i/o scheduler behaves
	 */
	int fifo_expire[2];
	int fifo_batch;
	int writes_starved;
	int front_merges;

	spinlock_t lock;
	spinlock_t zone_lock;
	struct list_head dispatch;
};

static inline struct rb_root *
deadline_rb_root(struct deadline_data *dd, struct request *rq)
{
	return &dd->sort_list[rq_data_dir(rq)];
}

/*
 * get the request after `rq' in sector-sorted order
 */
static inline struct request *
deadline_latter_request(struct request *rq)
{
	struct rb_node *node = rb_next(&rq->rb_node);

	if (node)
		return rb_entry_rq(node);

	return NULL;
}

static void
deadline_add_rq_rb(struct deadline_data *dd, struct request *rq)
{
	struct rb_root *root = deadline_rb_root(dd, rq);

	elv_rb_add(root, rq);
}

static inline void
deadline_del_rq_rb(struct deadline_data *dd, struct request *rq)
{
	const int data_dir = rq_data_dir(rq);

	if (dd->next_rq[data_dir] == rq)
		dd->next_rq[data_dir] = deadline_latter_request(rq);

	elv_rb_del(deadline_rb_root(dd, rq), rq);
}

/*
 * remove rq from rbtree and fifo.
 */
static void deadline_remove_request(struct request_queue *q, struct request *rq)
{
	struct deadline_data *dd = q->elevator->elevator_data;

	list_del_init(&rq->queuelist);

	/*
	 * We might not be on the rbtree, if we are doing an insert merge
	 */
	if (!RB_EMPTY_NODE(&rq->rb_node))
		deadline_del_rq_rb(dd, rq);

	elv_rqhash_del(q, rq);
	if (q->last_merge == rq)
		q->last_merge = NULL;
}

static void dd_request_merged(struct request_queue *q, struct request *req,
			      enum elv_merge type)
{
	struct deadline_data *dd = q->elevator->elevator_data;

	/*
	 * if the merge was a front merge, we need to reposition request
	 */
	if (type == ELEVATOR_FRONT_MERGE) {
		elv_rb_del(deadline_rb_root(dd, req), req);
		deadline_add_rq_rb(dd, req);
	}
}

static void dd_merged_requests(struct request_queue *q, struct request *req,
			       struct request *next)
{
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
	deadline_remove_request(q, next);
}

/*
 * move an entry to dispatch queue
 */
static void
deadline_move_request(struct deadline_data *dd, struct request *rq)
{
	const int data_dir = rq_data_dir(rq);

	dd->next_rq[READ] = NULL;
	dd->next_rq[WRITE] = NULL;
	dd->next_rq[data_dir] = deadline_latter_request(rq);

	/*
	 * take it off the sort and fifo list
	 */
	deadline_remove_request(rq->q, rq);
}

/*
 * deadline_check_fifo returns 0 if there are no expired requests on the fifo,
 * 1 otherwise. Requires !list_empty(&dd->fifo_list[data_dir])
 */
static inline int deadline_check_fifo(struct deadline_data *dd, int ddir)
{
	struct request *rq = rq_entry_fifo(dd->fifo_list[ddir].next);

	/*
	 * rq is expired!
	 */
	if (time_after_eq(jiffies, (unsigned long)rq->fifo_time))
		return 1;

	return 0;
}

/*
 * For the specified data direction, return the next request to
 * dispatch using arrival ordered lists.
 */
static struct request *
deadline_fifo_request(struct deadline_data *dd, int data_dir)
{
	struct request *rq;
	unsigned long flags;

	if (WARN_ON_ONCE(data_dir != READ && data_dir != WRITE))
		return NULL;

	if (list_empty(&dd->fifo_list[data_dir]))
		return NULL;

	rq = rq_entry_fifo(dd->fifo_list[data_dir].next);
	if (data_dir == READ || !blk_queue_is_zoned(rq->q))
		return rq;

	/*
	 * Look for a write request that can be dispatched, that is one with
	 * an unlocked target zone.
	 */
	spin_lock_irqsave(&dd->zone_lock, flags);
	list_for_each_entry(rq, &dd->fifo_list[WRITE], queuelist) {
		if (blk_req_can_dispatch_to_zone(rq))
			goto out;
	}
	rq = NULL;
out:
	spin_unlock_irqrestore(&dd->zone_lock, flags);

	return rq;
}

/*
 * For the specified data direction, return the next request to
 * dispatch using sector position sorted lists.
 */
static struct request *
deadline_next_request(struct deadline_data *dd, int data_dir)
{
	struct request *rq;
	unsigned long flags;

	if (WARN_ON_ONCE(data_dir != READ && data_dir != WRITE))
		return NULL;

	rq = dd->next_rq[data_dir];
	if (!rq)
		return NULL;

	if (data_dir == READ || !blk_queue_is_zoned(rq->q))
		return rq;

	/*
	 * Look for a write request that can be dispatched, that is one with
	 * an unlocked target zone.
	 */
	spin_lock_irqsave(&dd->zone_lock, flags);
	while (rq) {
		if (blk_req_can_dispatch_to_zone(rq))
			break;
		rq = deadline_latter_request(rq);
	}
	spin_unlock_irqrestore(&dd->zone_lock, flags);

	return rq;
}

/*
 * deadline_dispatch_requests selects the best request according to
 * read/write expire, fifo_batch, etc
 */
static struct request *__dd_dispatch_request(struct deadline_data *dd)
{
	struct request *rq, *next_rq;
	bool reads, writes;
	int data_dir;

	if (!list_empty(&dd->dispatch)) {
		rq = list_first_entry(&dd->dispatch, struct request, queuelist);
		list_del_init(&rq->queuelist);
		goto done;
	}

	reads = !list_empty(&dd->fifo_list[READ]);
	writes = !list_empty(&dd->fifo_list[WRITE]);

	/*
	 * batches are currently reads XOR writes
	 */
	rq = deadline_next_request(dd, WRITE);
	if (!rq)
		rq = deadline_next_request(dd, READ);

	if (rq && dd->batching < dd->fifo_batch)
		/* we have a next request are still entitled to batch */
		goto dispatch_request;

	/*
	 * at this point we are not running a batch. select the appropriate
	 * data direction (read / write)
	 */

	if (reads) {
		BUG_ON(RB_EMPTY_ROOT(&dd->sort_list[READ]));

		if (deadline_fifo_request(dd, WRITE) &&
		    (dd->starved++ >= dd->writes_starved))
			goto dispatch_writes;

		data_dir = READ;

		goto dispatch_find_request;
	}

	/*
	 * there are either no reads or writes have been starved
	 */

	if (writes) {
dispatch_writes:
		BUG_ON(RB_EMPTY_ROOT(&dd->sort_list[WRITE]));

		dd->starved = 0;

		data_dir = WRITE;

		goto dispatch_find_request;
	}

	return NULL;

dispatch_find_request:
	/*
	 * we are not running a batch, find best request for selected data_dir
	 */
	next_rq = deadline_next_request(dd, data_dir);
	if (deadline_check_fifo(dd, data_dir) || !next_rq) {
		/*
		 * A deadline has expired, the last request was in the other
		 * direction, or we have run out of higher-sectored requests.
		 * Start again from the request with the earliest expiry time.
		 */
		rq = deadline_fifo_request(dd, data_dir);
	} else {
		/*
		 * The last req was the same dir and we have a next request in
		 * sort order. No expired requests so continue on from here.
		 */
		rq = next_rq;
	}

	/*
	 * For a zoned block device, if we only have writes queued and none of
	 * them can be dispatched, rq will be NULL.
	 */
	if (!rq)
		return NULL;

	dd->batching = 0;

dispatch_request:
	/*
	 * rq is the selected appropriate request.
	 */
	dd->batching++;
	deadline_move_request(dd, rq);
done:
	/*
	 * If the request needs its target zone locked, do it.
	 */
	blk_req_zone_write_lock(rq);
	rq->rq_flags |= RQF_STARTED;
	return rq;
}

/*
 * One confusing aspect here is that we get called for a specific
 * hardware queue, but we return a request that may not be for a
 * different hardware queue. This is because mq-deadline has shared
 * state for all hardware queues, in terms of sorting, FIFOs, etc.
 */
static struct request *dd_dispatch_request(struct blk_mq_hw_ctx *hctx)
{
	struct deadline_data *dd = hctx->queue->elevator->elevator_data;
	struct request *rq;

	spin_lock(&dd->lock);
	rq = __dd_dispatch_request(dd);
	spin_unlock(&dd->lock);

	return rq;
}

static void dd_exit_queue(struct elevator_queue *e)
{
	struct deadline_data *dd = e->elevator_data;

	BUG_ON(!list_empty(&dd->fifo_list[READ]));
	BUG_ON(!list_empty(&dd->fifo_list[WRITE]));

	kfree(dd);
}

/*
 * initialize elevator private data (deadline_data).
 */
static int dd_init_queue(struct request_queue *q, struct elevator_type *e)
{
	struct deadline_data *dd;
	struct elevator_queue *eq;

	eq = elevator_alloc(q, e);
	if (!eq)
		return -ENOMEM;

	dd = kzalloc_node(sizeof(*dd), GFP_KERNEL, q->node);
	if (!dd) {
		kobject_put(&eq->kobj);
		return -ENOMEM;
	}
	eq->elevator_data = dd;

	INIT_LIST_HEAD(&dd->fifo_list[READ]);
	INIT_LIST_HEAD(&dd->fifo_list[WRITE]);
	dd->sort_list[READ] = RB_ROOT;
	dd->sort_list[WRITE] = RB_ROOT;
	dd->fifo_expire[READ] = read_expire;
	dd->fifo_expire[WRITE] = write_expire;
	dd->writes_starved = writes_starved;
	dd->front_merges = 1;
	dd->fifo_batch = fifo_batch;
	spin_lock_init(&dd->lock);
	spin_lock_init(&dd->zone_lock);
	INIT_LIST_HEAD(&dd->dispatch);

	q->elevator = eq;
	return 0;
}

static int dd_request_merge(struct request_queue *q, struct request **rq,
			    struct bio *bio)
{
	struct deadline_data *dd = q->elevator->elevator_data;
	sector_t sector = bio_end_sector(bio);
	struct request *__rq;

	if (!dd->front_merges)
		return ELEVATOR_NO_MERGE;

	__rq = elv_rb_find(&dd->sort_list[bio_data_dir(bio)], sector);
	if (__rq) {
		BUG_ON(sector != blk_rq_pos(__rq));

		if (elv_bio_merge_ok(__rq, bio)) {
			*rq = __rq;
			return ELEVATOR_FRONT_MERGE;
		}
	}

	return ELEVATOR_NO_MERGE;
}

static bool dd_bio_merge(struct blk_mq_hw_ctx *hctx, struct bio *bio)
{
	struct request_queue *q = hctx->queue;
	struct deadline_data *dd = q->elevator->elevator_data;
	struct request *free = NULL;
	bool ret;

	spin_lock(&dd->lock);
	ret = blk_mq_sched_try_merge(q, bio, &free);
	spin_unlock(&dd->lock);

	if (free)
		blk_mq_free_request(free);

	return ret;
}

/*
 * add rq to rbtree and fifo
 */
static void dd_insert_request(struct blk_mq_hw_ctx *hctx, struct request *rq,
			      bool at_head)
{
	struct request_queue *q = hctx->queue;
	struct deadline_data *dd = q->elevator->elevator_data;
	const int data_dir = rq_data_dir(rq);

	/*
	 * This may be a requeue of a write request that has locked its
	 * target zone. If it is the case, this releases the zone lock.
	 */
	blk_req_zone_write_unlock(rq);

	if (blk_mq_sched_try_insert_merge(q, rq))
		return;

	blk_mq_sched_request_inserted(rq);

	if (at_head || blk_rq_is_passthrough(rq)) {
		if (at_head)
			list_add(&rq->queuelist, &dd->dispatch);
		else
			list_add_tail(&rq->queuelist, &dd->dispatch);
	} else {
		deadline_add_rq_rb(dd, rq);

		if (rq_mergeable(rq)) {
			elv_rqhash_add(q, rq);
			if (!q->last_merge)
				q->last_merge = rq;
		}

		/*
		 * set expire time and add to fifo list
		 */
		rq->fifo_time = jiffies + dd->fifo_expire[data_dir];
		list_add_tail(&rq->queuelist, &dd->fifo_list[data_dir]);
	}
}

static void dd_insert_requests(struct blk_mq_hw_ctx *hctx,
			       struct list_head *list, bool at_head)
{
	struct request_queue *q = hctx->queue;
	struct deadline_data *dd = q->elevator->elevator_data;

	spin_lock(&dd->lock);
	while (!list_empty(list)) {
		struct request *rq;

		rq = list_first_entry(list, struct request, queuelist);
		list_del_init(&rq->queuelist);
		dd_insert_request(hctx, rq, at_head);
	}
	spin_unlock(&dd->lock);
}

/*
 * Nothing to do here. This is defined only to ensure that .finish_request
 * method is called upon request completion.
 */
static void dd_prepare_request(struct request *rq, struct bio *bio)
{
}

/*
 * For zoned block devices, write unlock the target zone of
 * completed write requests. Do this while holding the zone lock
 * spinlock so that the zone is never unlocked while deadline_fifo_request()
 * or deadline_next_request() are executing. This function is called for
 * all requests, whether or not these requests complete successfully.
 */
static void dd_finish_request(struct request *rq)
{
	struct request_queue *q = rq->q;

	if (blk_queue_is_zoned(q)) {
		struct deadline_data *dd = q->elevator->elevator_data;
		unsigned long flags;

		spin_lock_irqsave(&dd->zone_lock, flags);
		blk_req_zone_write_unlock(rq);
		spin_unlock_irqrestore(&dd->zone_lock, flags);
	}
}

static bool dd_has_work(struct blk_mq_hw_ctx *hctx)
{
	struct deadline_data *dd = hctx->queue->elevator->elevator_data;

	return !list_empty_careful(&dd->dispatch) ||
		!list_empty_careful(&dd->fifo_list[0]) ||
		!list_empty_careful(&dd->fifo_list[1]);
}

/*
 * sysfs parts below
 */
static ssize_t
deadline_var_show(int var, char *page)
{
	return sprintf(page, "%d\n", var);
}

static void
deadline_var_store(int *var, const char *page)
{
	char *p = (char *) page;

	*var = simple_strtol(p, &p, 10);
}

#define SHOW_FUNCTION(__FUNC, __VAR, __CONV)				\
static ssize_t __FUNC(struct elevator_queue *e, char *page)		\
{									\
	struct deadline_data *dd = e->elevator_data;			\
	int __data = __VAR;						\
	if (__CONV)							\
		__data = jiffies_to_msecs(__data);			\
	return deadline_var_show(__data, (page));			\
}
SHOW_FUNCTION(deadline_read_expire_show, dd->fifo_expire[READ], 1);
SHOW_FUNCTION(deadline_write_expire_show, dd->fifo_expire[WRITE], 1);
SHOW_FUNCTION(deadline_writes_starved_show, dd->writes_starved, 0);
SHOW_FUNCTION(deadline_front_merges_show, dd->front_merges, 0);
SHOW_FUNCTION(deadline_fifo_batch_show, dd->fifo_batch, 0);
#undef SHOW_FUNCTION

#define STORE_FUNCTION(__FUNC, __PTR, MIN, MAX, __CONV)			\
static ssize_t __FUNC(struct elevator_queue *e, const char *page, size_t count)	\
{									\
	struct deadline_data *dd = e->elevator_data;			\
	int __data;							\
	deadline_var_store(&__data, (page));				\
	if (__data < (MIN))						\
		__data = (MIN);						\
	else if (__data > (MAX))					\
		__data = (MAX);						\
	if (__CONV)							\
		*(__PTR) = msecs_to_jiffies(__data);			\
	else								\
		*(__PTR) = __data;					\
	return count;							\
}
STORE_FUNCTION(deadline_read_expire_store, &dd->fifo_expire[READ], 0, INT_MAX, 1);
STORE_FUNCTION(deadline_write_expire_store, &dd->fifo_expire[WRITE], 0, INT_MAX, 1);
STORE_FUNCTION(deadline_writes_starved_store, &dd->writes_starved, INT_MIN, INT_MAX, 0);
STORE_FUNCTION(deadline_front_merges_store, &dd->front_merges, 0, 1, 0);
STORE_FUNCTION(deadline_fifo_batch_store, &dd->fifo_batch, 0, INT_MAX, 0);
#undef STORE_FUNCTION

#define DD_ATTR(name) \
	__ATTR(name, S_IRUGO|S_IWUSR, deadline_##name##_show, \
				      deadline_##name##_store)

static struct elv_fs_entry deadline_attrs[] = {
	DD_ATTR(read_expire),
	DD_ATTR(write_expire),
	DD_ATTR(writes_starved),
	DD_ATTR(front_merges),
	DD_ATTR(fifo_batch),
	__ATTR_NULL
};

#ifdef CONFIG_BLK_DEBUG_FS
#define DEADLINE_DEBUGFS_DDIR_ATTRS(ddir, name)				\
static void *deadline_##name##_fifo_start(struct seq_file *m,		\
					  loff_t *pos)			\
	__acquires(&dd->lock)						\
{									\
	struct request_queue *q = m->private;				\
	struct deadline_data *dd = q->elevator->elevator_data;		\
									\
	spin_lock(&dd->lock);						\
	return seq_list_start(&dd->fifo_list[ddir], *pos);		\
}									\
									\
static void *deadline_##name##_fifo_next(struct seq_file *m, void *v,	\
					 loff_t *pos)			\
{									\
	struct request_queue *q = m->private;				\
	struct deadline_data *dd = q->elevator->elevator_data;		\
									\
	return seq_list_next(v, &dd->fifo_list[ddir], pos);		\
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
	struct request *rq = dd->next_rq[ddir];				\
									\
	if (rq)								\
		__blk_mq_debugfs_rq_show(m, rq);			\
	return 0;							\
}
DEADLINE_DEBUGFS_DDIR_ATTRS(READ, read)
DEADLINE_DEBUGFS_DDIR_ATTRS(WRITE, write)
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

static void *deadline_dispatch_start(struct seq_file *m, loff_t *pos)
	__acquires(&dd->lock)
{
	struct request_queue *q = m->private;
	struct deadline_data *dd = q->elevator->elevator_data;

	spin_lock(&dd->lock);
	return seq_list_start(&dd->dispatch, *pos);
}

static void *deadline_dispatch_next(struct seq_file *m, void *v, loff_t *pos)
{
	struct request_queue *q = m->private;
	struct deadline_data *dd = q->elevator->elevator_data;

	return seq_list_next(v, &dd->dispatch, pos);
}

static void deadline_dispatch_stop(struct seq_file *m, void *v)
	__releases(&dd->lock)
{
	struct request_queue *q = m->private;
	struct deadline_data *dd = q->elevator->elevator_data;

	spin_unlock(&dd->lock);
}

static const struct seq_operations deadline_dispatch_seq_ops = {
	.start	= deadline_dispatch_start,
	.next	= deadline_dispatch_next,
	.stop	= deadline_dispatch_stop,
	.show	= blk_mq_debugfs_rq_show,
};

#define DEADLINE_QUEUE_DDIR_ATTRS(name)						\
	{#name "_fifo_list", 0400, .seq_ops = &deadline_##name##_fifo_seq_ops},	\
	{#name "_next_rq", 0400, deadline_##name##_next_rq_show}
static const struct blk_mq_debugfs_attr deadline_queue_debugfs_attrs[] = {
	DEADLINE_QUEUE_DDIR_ATTRS(read),
	DEADLINE_QUEUE_DDIR_ATTRS(write),
	{"batching", 0400, deadline_batching_show},
	{"starved", 0400, deadline_starved_show},
	{"dispatch", 0400, .seq_ops = &deadline_dispatch_seq_ops},
	{},
};
#undef DEADLINE_QUEUE_DDIR_ATTRS
#endif

static struct elevator_type mq_deadline = {
	.ops.mq = {
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
		.init_sched		= dd_init_queue,
		.exit_sched		= dd_exit_queue,
	},

	.uses_mq	= true,
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

MODULE_AUTHOR("Jens Axboe");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MQ deadline IO scheduler");
