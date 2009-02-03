/*
 *  CFQ, or complete fairness queueing, disk scheduler.
 *
 *  Based on ideas from a previously unfinished io
 *  scheduler (round robin per-process disk scheduling) and Andrea Arcangeli.
 *
 *  Copyright (C) 2003 Jens Axboe <axboe@kernel.dk>
 */
#include <linux/module.h>
#include <linux/blkdev.h>
#include <linux/elevator.h>
#include <linux/rbtree.h>
#include <linux/ioprio.h>
#include <linux/blktrace_api.h>

/*
 * tunables
 */
/* max queue in one round of service */
static const int cfq_quantum = 4;
static const int cfq_fifo_expire[2] = { HZ / 4, HZ / 8 };
/* maximum backwards seek, in KiB */
static const int cfq_back_max = 16 * 1024;
/* penalty of a backwards seek */
static const int cfq_back_penalty = 2;
static const int cfq_slice_sync = HZ / 10;
static int cfq_slice_async = HZ / 25;
static const int cfq_slice_async_rq = 2;
static int cfq_slice_idle = HZ / 125;

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

#define RQ_CIC(rq)		\
	((struct cfq_io_context *) (rq)->elevator_private)
#define RQ_CFQQ(rq)		(struct cfq_queue *) ((rq)->elevator_private2)

static struct kmem_cache *cfq_pool;
static struct kmem_cache *cfq_ioc_pool;

static DEFINE_PER_CPU(unsigned long, ioc_count);
static struct completion *ioc_gone;
static DEFINE_SPINLOCK(ioc_gone_lock);

#define CFQ_PRIO_LISTS		IOPRIO_BE_NR
#define cfq_class_idle(cfqq)	((cfqq)->ioprio_class == IOPRIO_CLASS_IDLE)
#define cfq_class_rt(cfqq)	((cfqq)->ioprio_class == IOPRIO_CLASS_RT)

#define ASYNC			(0)
#define SYNC			(1)

#define sample_valid(samples)	((samples) > 80)

/*
 * Most of our rbtree usage is for sorting with min extraction, so
 * if we cache the leftmost node we don't have to walk down the tree
 * to find it. Idea borrowed from Ingo Molnars CFS scheduler. We should
 * move this into the elevator for the rq sorting as well.
 */
struct cfq_rb_root {
	struct rb_root rb;
	struct rb_node *left;
};
#define CFQ_RB_ROOT	(struct cfq_rb_root) { RB_ROOT, NULL, }

/*
 * Per block device queue structure
 */
struct cfq_data {
	struct request_queue *queue;

	/*
	 * rr list of queues with requests and the count of them
	 */
	struct cfq_rb_root service_tree;
	unsigned int busy_queues;
	/*
	 * Used to track any pending rt requests so we can pre-empt current
	 * non-RT cfqq in service when this value is non-zero.
	 */
	unsigned int busy_rt_queues;

	int rq_in_driver;
	int sync_flight;

	/*
	 * queue-depth detection
	 */
	int rq_queued;
	int hw_tag;
	int hw_tag_samples;
	int rq_in_driver_peak;

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
	unsigned long last_end_request;

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

	struct list_head cic_list;
};

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
};

enum cfqq_state_flags {
	CFQ_CFQQ_FLAG_on_rr = 0,	/* on round-robin busy list */
	CFQ_CFQQ_FLAG_wait_request,	/* waiting for a request */
	CFQ_CFQQ_FLAG_must_alloc,	/* must be allowed rq alloc */
	CFQ_CFQQ_FLAG_must_alloc_slice,	/* per-slice must_alloc flag */
	CFQ_CFQQ_FLAG_must_dispatch,	/* must dispatch, even if expired */
	CFQ_CFQQ_FLAG_fifo_expire,	/* FIFO checked in this slice */
	CFQ_CFQQ_FLAG_idle_window,	/* slice idling enabled */
	CFQ_CFQQ_FLAG_prio_changed,	/* task priority has changed */
	CFQ_CFQQ_FLAG_queue_new,	/* queue never been serviced */
	CFQ_CFQQ_FLAG_slice_new,	/* no requests dispatched in slice */
	CFQ_CFQQ_FLAG_sync,		/* synchronous queue */
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
CFQ_CFQQ_FNS(must_alloc);
CFQ_CFQQ_FNS(must_alloc_slice);
CFQ_CFQQ_FNS(must_dispatch);
CFQ_CFQQ_FNS(fifo_expire);
CFQ_CFQQ_FNS(idle_window);
CFQ_CFQQ_FNS(prio_changed);
CFQ_CFQQ_FNS(queue_new);
CFQ_CFQQ_FNS(slice_new);
CFQ_CFQQ_FNS(sync);
#undef CFQ_CFQQ_FNS

#define cfq_log_cfqq(cfqd, cfqq, fmt, args...)	\
	blk_add_trace_msg((cfqd)->queue, "cfq%d " fmt, (cfqq)->pid, ##args)
#define cfq_log(cfqd, fmt, args...)	\
	blk_add_trace_msg((cfqd)->queue, "cfq " fmt, ##args)

static void cfq_dispatch_insert(struct request_queue *, struct request *);
static struct cfq_queue *cfq_get_queue(struct cfq_data *, int,
				       struct io_context *, gfp_t);
static struct cfq_io_context *cfq_cic_lookup(struct cfq_data *,
						struct io_context *);

static inline struct cfq_queue *cic_to_cfqq(struct cfq_io_context *cic,
					    int is_sync)
{
	return cic->cfqq[!!is_sync];
}

static inline void cic_set_cfqq(struct cfq_io_context *cic,
				struct cfq_queue *cfqq, int is_sync)
{
	cic->cfqq[!!is_sync] = cfqq;
}

/*
 * We regard a request as SYNC, if it's either a read or has the SYNC bit
 * set (in which case it could also be direct WRITE).
 */
static inline int cfq_bio_sync(struct bio *bio)
{
	if (bio_data_dir(bio) == READ || bio_sync(bio))
		return 1;

	return 0;
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

	return !cfqd->busy_queues;
}

/*
 * Scale schedule slice based on io priority. Use the sync time slice only
 * if a queue is marked sync and has sync io queued. A sync queue with async
 * io only, should not get full sync slice length.
 */
static inline int cfq_prio_slice(struct cfq_data *cfqd, int sync,
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

static inline void
cfq_set_prio_slice(struct cfq_data *cfqd, struct cfq_queue *cfqq)
{
	cfqq->slice_end = cfq_prio_to_slice(cfqd, cfqq) + jiffies;
	cfq_log_cfqq(cfqd, cfqq, "set_slice=%lu", cfqq->slice_end - jiffies);
}

/*
 * We need to wrap this check in cfq_cfqq_slice_new(), since ->slice_end
 * isn't valid until the first request from the dispatch is activated
 * and the slice time set.
 */
static inline int cfq_slice_used(struct cfq_queue *cfqq)
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
cfq_choose_req(struct cfq_data *cfqd, struct request *rq1, struct request *rq2)
{
	sector_t last, s1, s2, d1 = 0, d2 = 0;
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
	if (rq_is_meta(rq1) && !rq_is_meta(rq2))
		return rq1;
	else if (rq_is_meta(rq2) && !rq_is_meta(rq1))
		return rq2;

	s1 = rq1->sector;
	s2 = rq2->sector;

	last = cfqd->last_position;

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
	if (!root->left)
		root->left = rb_first(&root->rb);

	if (root->left)
		return rb_entry(root->left, struct cfq_queue, rb_node);

	return NULL;
}

static void cfq_rb_erase(struct rb_node *n, struct cfq_rb_root *root)
{
	if (root->left == n)
		root->left = NULL;

	rb_erase(n, &root->rb);
	RB_CLEAR_NODE(n);
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

	return cfq_choose_req(cfqd, next, prev);
}

static unsigned long cfq_slice_offset(struct cfq_data *cfqd,
				      struct cfq_queue *cfqq)
{
	/*
	 * just an approximation, should be ok.
	 */
	return (cfqd->busy_queues - 1) * (cfq_prio_slice(cfqd, 1, 0) -
		       cfq_prio_slice(cfqd, cfq_cfqq_sync(cfqq), cfqq->ioprio));
}

/*
 * The cfqd->service_tree holds all pending cfq_queue's that have
 * requests waiting to be processed. It is sorted in the order that
 * we will service the queues.
 */
static void cfq_service_tree_add(struct cfq_data *cfqd,
				    struct cfq_queue *cfqq, int add_front)
{
	struct rb_node **p, *parent;
	struct cfq_queue *__cfqq;
	unsigned long rb_key;
	int left;

	if (cfq_class_idle(cfqq)) {
		rb_key = CFQ_IDLE_DELAY;
		parent = rb_last(&cfqd->service_tree.rb);
		if (parent && parent != &cfqq->rb_node) {
			__cfqq = rb_entry(parent, struct cfq_queue, rb_node);
			rb_key += __cfqq->rb_key;
		} else
			rb_key += jiffies;
	} else if (!add_front) {
		rb_key = cfq_slice_offset(cfqd, cfqq) + jiffies;
		rb_key += cfqq->slice_resid;
		cfqq->slice_resid = 0;
	} else
		rb_key = 0;

	if (!RB_EMPTY_NODE(&cfqq->rb_node)) {
		/*
		 * same position, nothing more to do
		 */
		if (rb_key == cfqq->rb_key)
			return;

		cfq_rb_erase(&cfqq->rb_node, &cfqd->service_tree);
	}

	left = 1;
	parent = NULL;
	p = &cfqd->service_tree.rb.rb_node;
	while (*p) {
		struct rb_node **n;

		parent = *p;
		__cfqq = rb_entry(parent, struct cfq_queue, rb_node);

		/*
		 * sort RT queues first, we always want to give
		 * preference to them. IDLE queues goes to the back.
		 * after that, sort on the next service time.
		 */
		if (cfq_class_rt(cfqq) > cfq_class_rt(__cfqq))
			n = &(*p)->rb_left;
		else if (cfq_class_rt(cfqq) < cfq_class_rt(__cfqq))
			n = &(*p)->rb_right;
		else if (cfq_class_idle(cfqq) < cfq_class_idle(__cfqq))
			n = &(*p)->rb_left;
		else if (cfq_class_idle(cfqq) > cfq_class_idle(__cfqq))
			n = &(*p)->rb_right;
		else if (rb_key < __cfqq->rb_key)
			n = &(*p)->rb_left;
		else
			n = &(*p)->rb_right;

		if (n == &(*p)->rb_right)
			left = 0;

		p = n;
	}

	if (left)
		cfqd->service_tree.left = &cfqq->rb_node;

	cfqq->rb_key = rb_key;
	rb_link_node(&cfqq->rb_node, parent, p);
	rb_insert_color(&cfqq->rb_node, &cfqd->service_tree.rb);
}

/*
 * Update cfqq's position in the service tree.
 */
static void cfq_resort_rr_list(struct cfq_data *cfqd, struct cfq_queue *cfqq)
{
	/*
	 * Resorting requires the cfqq to be on the RR list already.
	 */
	if (cfq_cfqq_on_rr(cfqq))
		cfq_service_tree_add(cfqd, cfqq, 0);
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
	if (cfq_class_rt(cfqq))
		cfqd->busy_rt_queues++;

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

	if (!RB_EMPTY_NODE(&cfqq->rb_node))
		cfq_rb_erase(&cfqq->rb_node, &cfqd->service_tree);

	BUG_ON(!cfqd->busy_queues);
	cfqd->busy_queues--;
	if (cfq_class_rt(cfqq))
		cfqd->busy_rt_queues--;
}

/*
 * rb tree support functions
 */
static void cfq_del_rq_rb(struct request *rq)
{
	struct cfq_queue *cfqq = RQ_CFQQ(rq);
	struct cfq_data *cfqd = cfqq->cfqd;
	const int sync = rq_is_sync(rq);

	BUG_ON(!cfqq->queued[sync]);
	cfqq->queued[sync]--;

	elv_rb_del(&cfqq->sort_list, rq);

	if (cfq_cfqq_on_rr(cfqq) && RB_EMPTY_ROOT(&cfqq->sort_list))
		cfq_del_cfqq_rr(cfqd, cfqq);
}

static void cfq_add_rq_rb(struct request *rq)
{
	struct cfq_queue *cfqq = RQ_CFQQ(rq);
	struct cfq_data *cfqd = cfqq->cfqd;
	struct request *__alias;

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
	cfqq->next_rq = cfq_choose_req(cfqd, cfqq->next_rq, rq);
	BUG_ON(!cfqq->next_rq);
}

static void cfq_reposition_rq_rb(struct cfq_queue *cfqq, struct request *rq)
{
	elv_rb_del(&cfqq->sort_list, rq);
	cfqq->queued[rq_is_sync(rq)]--;
	cfq_add_rq_rb(rq);
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

	cfqd->last_position = rq->hard_sector + rq->hard_nr_sectors;
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
	if (rq_is_meta(rq)) {
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

static void
cfq_merged_requests(struct request_queue *q, struct request *rq,
		    struct request *next)
{
	/*
	 * reposition in fifo if next is older than rq
	 */
	if (!list_empty(&rq->queuelist) && !list_empty(&next->queuelist) &&
	    time_before(next->start_time, rq->start_time))
		list_move(&rq->queuelist, &next->queuelist);

	cfq_remove_request(next);
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
		return 0;

	/*
	 * Lookup the cfqq that this bio will be queued with. Allow
	 * merge only if rq is queued there.
	 */
	cic = cfq_cic_lookup(cfqd, current->io_context);
	if (!cic)
		return 0;

	cfqq = cic_to_cfqq(cic, cfq_bio_sync(bio));
	if (cfqq == RQ_CFQQ(rq))
		return 1;

	return 0;
}

static void __cfq_set_active_queue(struct cfq_data *cfqd,
				   struct cfq_queue *cfqq)
{
	if (cfqq) {
		cfq_log_cfqq(cfqd, cfqq, "set_active");
		cfqq->slice_end = 0;
		cfq_clear_cfqq_must_alloc_slice(cfqq);
		cfq_clear_cfqq_fifo_expire(cfqq);
		cfq_mark_cfqq_slice_new(cfqq);
		cfq_clear_cfqq_queue_new(cfqq);
	}

	cfqd->active_queue = cfqq;
}

/*
 * current cfqq expired its slice (or was too idle), select new one
 */
static void
__cfq_slice_expired(struct cfq_data *cfqd, struct cfq_queue *cfqq,
		    int timed_out)
{
	cfq_log_cfqq(cfqd, cfqq, "slice expired t=%d", timed_out);

	if (cfq_cfqq_wait_request(cfqq))
		del_timer(&cfqd->idle_slice_timer);

	cfq_clear_cfqq_must_dispatch(cfqq);
	cfq_clear_cfqq_wait_request(cfqq);

	/*
	 * store what was left of this slice, if the queue idled/timed out
	 */
	if (timed_out && !cfq_cfqq_slice_new(cfqq)) {
		cfqq->slice_resid = cfqq->slice_end - jiffies;
		cfq_log_cfqq(cfqd, cfqq, "resid=%ld", cfqq->slice_resid);
	}

	cfq_resort_rr_list(cfqd, cfqq);

	if (cfqq == cfqd->active_queue)
		cfqd->active_queue = NULL;

	if (cfqd->active_cic) {
		put_io_context(cfqd->active_cic->ioc);
		cfqd->active_cic = NULL;
	}
}

static inline void cfq_slice_expired(struct cfq_data *cfqd, int timed_out)
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
	if (RB_EMPTY_ROOT(&cfqd->service_tree.rb))
		return NULL;

	return cfq_rb_first(&cfqd->service_tree);
}

/*
 * Get and set a new active queue for service.
 */
static struct cfq_queue *cfq_set_active_queue(struct cfq_data *cfqd)
{
	struct cfq_queue *cfqq;

	cfqq = cfq_get_next_queue(cfqd);
	__cfq_set_active_queue(cfqd, cfqq);
	return cfqq;
}

static inline sector_t cfq_dist_from_last(struct cfq_data *cfqd,
					  struct request *rq)
{
	if (rq->sector >= cfqd->last_position)
		return rq->sector - cfqd->last_position;
	else
		return cfqd->last_position - rq->sector;
}

static inline int cfq_rq_close(struct cfq_data *cfqd, struct request *rq)
{
	struct cfq_io_context *cic = cfqd->active_cic;

	if (!sample_valid(cic->seek_samples))
		return 0;

	return cfq_dist_from_last(cfqd, rq) <= cic->seek_mean;
}

static int cfq_close_cooperator(struct cfq_data *cfq_data,
				struct cfq_queue *cfqq)
{
	/*
	 * We should notice if some of the queues are cooperating, eg
	 * working closely on the same area of the disk. In that case,
	 * we can group them together and don't waste time idling.
	 */
	return 0;
}

#define CIC_SEEKY(cic) ((cic)->seek_mean > (8 * 1024))

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
	if (!cfqd->cfq_slice_idle || !cfq_cfqq_idle_window(cfqq))
		return;

	/*
	 * still requests with the driver, don't idle
	 */
	if (cfqd->rq_in_driver)
		return;

	/*
	 * task has exited, don't wait
	 */
	cic = cfqd->active_cic;
	if (!cic || !atomic_read(&cic->ioc->nr_tasks))
		return;

	/*
	 * See if this prio level has a good candidate
	 */
	if (cfq_close_cooperator(cfqd, cfqq) &&
	    (sample_valid(cic->ttime_samples) && cic->ttime_mean > 2))
		return;

	cfq_mark_cfqq_must_dispatch(cfqq);
	cfq_mark_cfqq_wait_request(cfqq);

	/*
	 * we don't want to idle for seeks, but we do want to allow
	 * fair distribution of slice time for a process doing back-to-back
	 * seeks. so allow a little bit of time for him to submit a new rq
	 */
	sl = cfqd->cfq_slice_idle;
	if (sample_valid(cic->seek_samples) && CIC_SEEKY(cic))
		sl = min(sl, msecs_to_jiffies(CFQ_MIN_TT));

	mod_timer(&cfqd->idle_slice_timer, jiffies + sl);
	cfq_log(cfqd, "arm_idle: %lu", sl);
}

/*
 * Move request from internal lists to the request queue dispatch list.
 */
static void cfq_dispatch_insert(struct request_queue *q, struct request *rq)
{
	struct cfq_data *cfqd = q->elevator->elevator_data;
	struct cfq_queue *cfqq = RQ_CFQQ(rq);

	cfq_log_cfqq(cfqd, cfqq, "dispatch_insert");

	cfq_remove_request(rq);
	cfqq->dispatched++;
	elv_dispatch_sort(q, rq);

	if (cfq_cfqq_sync(cfqq))
		cfqd->sync_flight++;
}

/*
 * return expired entry, or NULL to just start from scratch in rbtree
 */
static struct request *cfq_check_fifo(struct cfq_queue *cfqq)
{
	struct cfq_data *cfqd = cfqq->cfqd;
	struct request *rq;
	int fifo;

	if (cfq_cfqq_fifo_expire(cfqq))
		return NULL;

	cfq_mark_cfqq_fifo_expire(cfqq);

	if (list_empty(&cfqq->fifo))
		return NULL;

	fifo = cfq_cfqq_sync(cfqq);
	rq = rq_entry_fifo(cfqq->fifo.next);

	if (time_before(jiffies, rq->start_time + cfqd->cfq_fifo_expire[fifo]))
		rq = NULL;

	cfq_log_cfqq(cfqd, cfqq, "fifo=%p", rq);
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
 * Select a queue for service. If we have a current active queue,
 * check whether to continue servicing it, or retrieve and set a new one.
 */
static struct cfq_queue *cfq_select_queue(struct cfq_data *cfqd)
{
	struct cfq_queue *cfqq;

	cfqq = cfqd->active_queue;
	if (!cfqq)
		goto new_queue;

	/*
	 * The active queue has run out of time, expire it and select new.
	 */
	if (cfq_slice_used(cfqq))
		goto expire;

	/*
	 * If we have a RT cfqq waiting, then we pre-empt the current non-rt
	 * cfqq.
	 */
	if (!cfq_class_rt(cfqq) && cfqd->busy_rt_queues) {
		/*
		 * We simulate this as cfqq timed out so that it gets to bank
		 * the remaining of its time slice.
		 */
		cfq_log_cfqq(cfqd, cfqq, "preempt");
		cfq_slice_expired(cfqd, 1);
		goto new_queue;
	}

	/*
	 * The active queue has requests and isn't expired, allow it to
	 * dispatch.
	 */
	if (!RB_EMPTY_ROOT(&cfqq->sort_list))
		goto keep_queue;

	/*
	 * No requests pending. If the active queue still has requests in
	 * flight or is idling for a new request, allow either of these
	 * conditions to happen (or time out) before selecting a new queue.
	 */
	if (timer_pending(&cfqd->idle_slice_timer) ||
	    (cfqq->dispatched && cfq_cfqq_idle_window(cfqq))) {
		cfqq = NULL;
		goto keep_queue;
	}

expire:
	cfq_slice_expired(cfqd, 0);
new_queue:
	cfqq = cfq_set_active_queue(cfqd);
keep_queue:
	return cfqq;
}

/*
 * Dispatch some requests from cfqq, moving them to the request queue
 * dispatch list.
 */
static int
__cfq_dispatch_requests(struct cfq_data *cfqd, struct cfq_queue *cfqq,
			int max_dispatch)
{
	int dispatched = 0;

	BUG_ON(RB_EMPTY_ROOT(&cfqq->sort_list));

	do {
		struct request *rq;

		/*
		 * follow expired path, else get first next available
		 */
		rq = cfq_check_fifo(cfqq);
		if (rq == NULL)
			rq = cfqq->next_rq;

		/*
		 * finally, insert request into driver dispatch list
		 */
		cfq_dispatch_insert(cfqd->queue, rq);

		dispatched++;

		if (!cfqd->active_cic) {
			atomic_inc(&RQ_CIC(rq)->ioc->refcount);
			cfqd->active_cic = RQ_CIC(rq);
		}

		if (RB_EMPTY_ROOT(&cfqq->sort_list))
			break;

		/*
		 * If there is a non-empty RT cfqq waiting for current
		 * cfqq's timeslice to complete, pre-empt this cfqq
		 */
		if (!cfq_class_rt(cfqq) && cfqd->busy_rt_queues)
			break;

	} while (dispatched < max_dispatch);

	/*
	 * expire an async queue immediately if it has used up its slice. idle
	 * queue always expire after 1 dispatch round.
	 */
	if (cfqd->busy_queues > 1 && ((!cfq_cfqq_sync(cfqq) &&
	    dispatched >= cfq_prio_to_maxrq(cfqd, cfqq)) ||
	    cfq_class_idle(cfqq))) {
		cfqq->slice_end = jiffies + 1;
		cfq_slice_expired(cfqd, 0);
	}

	return dispatched;
}

static int __cfq_forced_dispatch_cfqq(struct cfq_queue *cfqq)
{
	int dispatched = 0;

	while (cfqq->next_rq) {
		cfq_dispatch_insert(cfqq->cfqd->queue, cfqq->next_rq);
		dispatched++;
	}

	BUG_ON(!list_empty(&cfqq->fifo));
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

	while ((cfqq = cfq_rb_first(&cfqd->service_tree)) != NULL)
		dispatched += __cfq_forced_dispatch_cfqq(cfqq);

	cfq_slice_expired(cfqd, 0);

	BUG_ON(cfqd->busy_queues);

	cfq_log(cfqd, "forced_dispatch=%d\n", dispatched);
	return dispatched;
}

static int cfq_dispatch_requests(struct request_queue *q, int force)
{
	struct cfq_data *cfqd = q->elevator->elevator_data;
	struct cfq_queue *cfqq;
	int dispatched;

	if (!cfqd->busy_queues)
		return 0;

	if (unlikely(force))
		return cfq_forced_dispatch(cfqd);

	dispatched = 0;
	while ((cfqq = cfq_select_queue(cfqd)) != NULL) {
		int max_dispatch;

		max_dispatch = cfqd->cfq_quantum;
		if (cfq_class_idle(cfqq))
			max_dispatch = 1;

		if (cfqq->dispatched >= max_dispatch && cfqd->busy_queues > 1)
			break;

		if (cfqd->sync_flight && !cfq_cfqq_sync(cfqq))
			break;

		cfq_clear_cfqq_must_dispatch(cfqq);
		cfq_clear_cfqq_wait_request(cfqq);
		del_timer(&cfqd->idle_slice_timer);

		dispatched += __cfq_dispatch_requests(cfqd, cfqq, max_dispatch);
	}

	cfq_log(cfqd, "dispatched=%d", dispatched);
	return dispatched;
}

/*
 * task holds one reference to the queue, dropped when task exits. each rq
 * in-flight on this queue also holds a reference, dropped when rq is freed.
 *
 * queue lock must be held here.
 */
static void cfq_put_queue(struct cfq_queue *cfqq)
{
	struct cfq_data *cfqd = cfqq->cfqd;

	BUG_ON(atomic_read(&cfqq->ref) <= 0);

	if (!atomic_dec_and_test(&cfqq->ref))
		return;

	cfq_log_cfqq(cfqd, cfqq, "put_queue");
	BUG_ON(rb_first(&cfqq->sort_list));
	BUG_ON(cfqq->allocated[READ] + cfqq->allocated[WRITE]);
	BUG_ON(cfq_cfqq_on_rr(cfqq));

	if (unlikely(cfqd->active_queue == cfqq)) {
		__cfq_slice_expired(cfqd, cfqq, 0);
		cfq_schedule_dispatch(cfqd);
	}

	kmem_cache_free(cfq_pool, cfqq);
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
	elv_ioc_count_dec(ioc_count);

	if (ioc_gone) {
		/*
		 * CFQ scheduler is exiting, grab exit lock and check
		 * the pending io context count. If it hits zero,
		 * complete ioc_gone and set it back to NULL
		 */
		spin_lock(&ioc_gone_lock);
		if (ioc_gone && !elv_ioc_count_read(ioc_count)) {
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

	BUG_ON(!cic->dead_key);

	spin_lock_irqsave(&ioc->lock, flags);
	radix_tree_delete(&ioc->radix_root, cic->dead_key);
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

static void cfq_exit_cfqq(struct cfq_data *cfqd, struct cfq_queue *cfqq)
{
	if (unlikely(cfqq == cfqd->active_queue)) {
		__cfq_slice_expired(cfqd, cfqq, 0);
		cfq_schedule_dispatch(cfqd);
	}

	cfq_put_queue(cfqq);
}

static void __cfq_exit_single_io_context(struct cfq_data *cfqd,
					 struct cfq_io_context *cic)
{
	struct io_context *ioc = cic->ioc;

	list_del_init(&cic->queue_list);

	/*
	 * Make sure key == NULL is seen for dead queues
	 */
	smp_wmb();
	cic->dead_key = (unsigned long) cic->key;
	cic->key = NULL;

	if (ioc->ioc_data == cic)
		rcu_assign_pointer(ioc->ioc_data, NULL);

	if (cic->cfqq[ASYNC]) {
		cfq_exit_cfqq(cfqd, cic->cfqq[ASYNC]);
		cic->cfqq[ASYNC] = NULL;
	}

	if (cic->cfqq[SYNC]) {
		cfq_exit_cfqq(cfqd, cic->cfqq[SYNC]);
		cic->cfqq[SYNC] = NULL;
	}
}

static void cfq_exit_single_io_context(struct io_context *ioc,
				       struct cfq_io_context *cic)
{
	struct cfq_data *cfqd = cic->key;

	if (cfqd) {
		struct request_queue *q = cfqd->queue;
		unsigned long flags;

		spin_lock_irqsave(q->queue_lock, flags);

		/*
		 * Ensure we get a fresh copy of the ->key to prevent
		 * race between exiting task and queue
		 */
		smp_read_barrier_depends();
		if (cic->key)
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
		elv_ioc_count_inc(ioc_count);
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
	struct cfq_data *cfqd = cic->key;
	struct cfq_queue *cfqq;
	unsigned long flags;

	if (unlikely(!cfqd))
		return;

	spin_lock_irqsave(cfqd->queue->queue_lock, flags);

	cfqq = cic->cfqq[ASYNC];
	if (cfqq) {
		struct cfq_queue *new_cfqq;
		new_cfqq = cfq_get_queue(cfqd, ASYNC, cic->ioc, GFP_ATOMIC);
		if (new_cfqq) {
			cic->cfqq[ASYNC] = new_cfqq;
			cfq_put_queue(cfqq);
		}
	}

	cfqq = cic->cfqq[SYNC];
	if (cfqq)
		cfq_mark_cfqq_prio_changed(cfqq);

	spin_unlock_irqrestore(cfqd->queue->queue_lock, flags);
}

static void cfq_ioc_set_ioprio(struct io_context *ioc)
{
	call_for_each_cic(ioc, changed_ioprio);
	ioc->ioprio_changed = 0;
}

static struct cfq_queue *
cfq_find_alloc_queue(struct cfq_data *cfqd, int is_sync,
		     struct io_context *ioc, gfp_t gfp_mask)
{
	struct cfq_queue *cfqq, *new_cfqq = NULL;
	struct cfq_io_context *cic;

retry:
	cic = cfq_cic_lookup(cfqd, ioc);
	/* cic always exists here */
	cfqq = cic_to_cfqq(cic, is_sync);

	if (!cfqq) {
		if (new_cfqq) {
			cfqq = new_cfqq;
			new_cfqq = NULL;
		} else if (gfp_mask & __GFP_WAIT) {
			/*
			 * Inform the allocator of the fact that we will
			 * just repeat this allocation if it fails, to allow
			 * the allocator to do whatever it needs to attempt to
			 * free memory.
			 */
			spin_unlock_irq(cfqd->queue->queue_lock);
			new_cfqq = kmem_cache_alloc_node(cfq_pool,
					gfp_mask | __GFP_NOFAIL | __GFP_ZERO,
					cfqd->queue->node);
			spin_lock_irq(cfqd->queue->queue_lock);
			goto retry;
		} else {
			cfqq = kmem_cache_alloc_node(cfq_pool,
					gfp_mask | __GFP_ZERO,
					cfqd->queue->node);
			if (!cfqq)
				goto out;
		}

		RB_CLEAR_NODE(&cfqq->rb_node);
		INIT_LIST_HEAD(&cfqq->fifo);

		atomic_set(&cfqq->ref, 0);
		cfqq->cfqd = cfqd;

		cfq_mark_cfqq_prio_changed(cfqq);
		cfq_mark_cfqq_queue_new(cfqq);

		cfq_init_prio_data(cfqq, ioc);

		if (is_sync) {
			if (!cfq_class_idle(cfqq))
				cfq_mark_cfqq_idle_window(cfqq);
			cfq_mark_cfqq_sync(cfqq);
		}
		cfqq->pid = current->pid;
		cfq_log_cfqq(cfqd, cfqq, "alloced");
	}

	if (new_cfqq)
		kmem_cache_free(cfq_pool, new_cfqq);

out:
	WARN_ON((gfp_mask & __GFP_WAIT) && !cfqq);
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
cfq_get_queue(struct cfq_data *cfqd, int is_sync, struct io_context *ioc,
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

	if (!cfqq) {
		cfqq = cfq_find_alloc_queue(cfqd, is_sync, ioc, gfp_mask);
		if (!cfqq)
			return NULL;
	}

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

	spin_lock_irqsave(&ioc->lock, flags);

	BUG_ON(ioc->ioc_data == cic);

	radix_tree_delete(&ioc->radix_root, (unsigned long) cfqd);
	hlist_del_rcu(&cic->cic_list);
	spin_unlock_irqrestore(&ioc->lock, flags);

	cfq_cic_free(cic);
}

static struct cfq_io_context *
cfq_cic_lookup(struct cfq_data *cfqd, struct io_context *ioc)
{
	struct cfq_io_context *cic;
	unsigned long flags;
	void *k;

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
		cic = radix_tree_lookup(&ioc->radix_root, (unsigned long) cfqd);
		rcu_read_unlock();
		if (!cic)
			break;
		/* ->key must be copied to avoid race with cfq_exit_queue() */
		k = cic->key;
		if (unlikely(!k)) {
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
						(unsigned long) cfqd, cic);
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
cfq_update_io_seektime(struct cfq_data *cfqd, struct cfq_io_context *cic,
		       struct request *rq)
{
	sector_t sdist;
	u64 total;

	if (cic->last_request_pos < rq->sector)
		sdist = rq->sector - cic->last_request_pos;
	else
		sdist = cic->last_request_pos - rq->sector;

	/*
	 * Don't allow the seek distance to get too large from the
	 * odd fragment, pagein, etc
	 */
	if (cic->seek_samples <= 60) /* second&third seek */
		sdist = min(sdist, (cic->seek_mean * 4) + 2*1024*1024);
	else
		sdist = min(sdist, (cic->seek_mean * 4)	+ 2*1024*64);

	cic->seek_samples = (7*cic->seek_samples + 256) / 8;
	cic->seek_total = (7*cic->seek_total + (u64)256*sdist) / 8;
	total = cic->seek_total + (cic->seek_samples/2);
	do_div(total, cic->seek_samples);
	cic->seek_mean = (sector_t)total;
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

	if (!atomic_read(&cic->ioc->nr_tasks) || !cfqd->cfq_slice_idle ||
	    (cfqd->hw_tag && CIC_SEEKY(cic)))
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
static int
cfq_should_preempt(struct cfq_data *cfqd, struct cfq_queue *new_cfqq,
		   struct request *rq)
{
	struct cfq_queue *cfqq;

	cfqq = cfqd->active_queue;
	if (!cfqq)
		return 0;

	if (cfq_slice_used(cfqq))
		return 1;

	if (cfq_class_idle(new_cfqq))
		return 0;

	if (cfq_class_idle(cfqq))
		return 1;

	/*
	 * if the new request is sync, but the currently running queue is
	 * not, let the sync request have priority.
	 */
	if (rq_is_sync(rq) && !cfq_cfqq_sync(cfqq))
		return 1;

	/*
	 * So both queues are sync. Let the new request get disk time if
	 * it's a metadata request and the current queue is doing regular IO.
	 */
	if (rq_is_meta(rq) && !cfqq->meta_pending)
		return 1;

	/*
	 * Allow an RT request to pre-empt an ongoing non-RT cfqq timeslice.
	 */
	if (cfq_class_rt(new_cfqq) && !cfq_class_rt(cfqq))
		return 1;

	if (!cfqd->active_cic || !cfq_cfqq_wait_request(cfqq))
		return 0;

	/*
	 * if this request is as-good as one we would expect from the
	 * current cfqq, let it preempt
	 */
	if (cfq_rq_close(cfqd, rq))
		return 1;

	return 0;
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
	if (rq_is_meta(rq))
		cfqq->meta_pending++;

	cfq_update_io_thinktime(cfqd, cic);
	cfq_update_io_seektime(cfqd, cic, rq);
	cfq_update_idle_window(cfqd, cfqq, cic);

	cic->last_request_pos = rq->sector + rq->nr_sectors;

	if (cfqq == cfqd->active_queue) {
		/*
		 * if we are waiting for a request for this queue, let it rip
		 * immediately and flag that we must not expire this queue
		 * just now
		 */
		if (cfq_cfqq_wait_request(cfqq)) {
			cfq_mark_cfqq_must_dispatch(cfqq);
			del_timer(&cfqd->idle_slice_timer);
			blk_start_queueing(cfqd->queue);
		}
	} else if (cfq_should_preempt(cfqd, cfqq, rq)) {
		/*
		 * not the active queue - expire current slice if it is
		 * idle and has expired it's mean thinktime or this new queue
		 * has some old slice time left and is of higher priority or
		 * this new queue is RT and the current one is BE
		 */
		cfq_preempt_queue(cfqd, cfqq);
		cfq_mark_cfqq_must_dispatch(cfqq);
		blk_start_queueing(cfqd->queue);
	}
}

static void cfq_insert_request(struct request_queue *q, struct request *rq)
{
	struct cfq_data *cfqd = q->elevator->elevator_data;
	struct cfq_queue *cfqq = RQ_CFQQ(rq);

	cfq_log_cfqq(cfqd, cfqq, "insert_request");
	cfq_init_prio_data(cfqq, RQ_CIC(rq)->ioc);

	cfq_add_rq_rb(rq);

	list_add_tail(&rq->queuelist, &cfqq->fifo);

	cfq_rq_enqueued(cfqd, cfqq, rq);
}

/*
 * Update hw_tag based on peak queue depth over 50 samples under
 * sufficient load.
 */
static void cfq_update_hw_tag(struct cfq_data *cfqd)
{
	if (cfqd->rq_in_driver > cfqd->rq_in_driver_peak)
		cfqd->rq_in_driver_peak = cfqd->rq_in_driver;

	if (cfqd->rq_queued <= CFQ_HW_QUEUE_MIN &&
	    cfqd->rq_in_driver <= CFQ_HW_QUEUE_MIN)
		return;

	if (cfqd->hw_tag_samples++ < 50)
		return;

	if (cfqd->rq_in_driver_peak >= CFQ_HW_QUEUE_MIN)
		cfqd->hw_tag = 1;
	else
		cfqd->hw_tag = 0;

	cfqd->hw_tag_samples = 0;
	cfqd->rq_in_driver_peak = 0;
}

static void cfq_completed_request(struct request_queue *q, struct request *rq)
{
	struct cfq_queue *cfqq = RQ_CFQQ(rq);
	struct cfq_data *cfqd = cfqq->cfqd;
	const int sync = rq_is_sync(rq);
	unsigned long now;

	now = jiffies;
	cfq_log_cfqq(cfqd, cfqq, "complete");

	cfq_update_hw_tag(cfqd);

	WARN_ON(!cfqd->rq_in_driver);
	WARN_ON(!cfqq->dispatched);
	cfqd->rq_in_driver--;
	cfqq->dispatched--;

	if (cfq_cfqq_sync(cfqq))
		cfqd->sync_flight--;

	if (!cfq_class_idle(cfqq))
		cfqd->last_end_request = now;

	if (sync)
		RQ_CIC(rq)->last_end_request = now;

	/*
	 * If this is the active queue, check if it needs to be expired,
	 * or if we want to idle in case it has no pending requests.
	 */
	if (cfqd->active_queue == cfqq) {
		if (cfq_cfqq_slice_new(cfqq)) {
			cfq_set_prio_slice(cfqd, cfqq);
			cfq_clear_cfqq_slice_new(cfqq);
		}
		if (cfq_slice_used(cfqq) || cfq_class_idle(cfqq))
			cfq_slice_expired(cfqd, 1);
		else if (sync && RB_EMPTY_ROOT(&cfqq->sort_list))
			cfq_arm_slice_timer(cfqd);
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
		 * check if we need to unboost the queue
		 */
		if (cfqq->ioprio_class != cfqq->org_ioprio_class)
			cfqq->ioprio_class = cfqq->org_ioprio_class;
		if (cfqq->ioprio != cfqq->org_ioprio)
			cfqq->ioprio = cfqq->org_ioprio;
	}
}

static inline int __cfq_may_queue(struct cfq_queue *cfqq)
{
	if ((cfq_cfqq_wait_request(cfqq) || cfq_cfqq_must_alloc(cfqq)) &&
	    !cfq_cfqq_must_alloc_slice(cfqq)) {
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

	cfqq = cic_to_cfqq(cic, rw & REQ_RW_SYNC);
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

		cfq_put_queue(cfqq);
	}
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
	const int is_sync = rq_is_sync(rq);
	struct cfq_queue *cfqq;
	unsigned long flags;

	might_sleep_if(gfp_mask & __GFP_WAIT);

	cic = cfq_get_io_context(cfqd, gfp_mask);

	spin_lock_irqsave(q->queue_lock, flags);

	if (!cic)
		goto queue_fail;

	cfqq = cic_to_cfqq(cic, is_sync);
	if (!cfqq) {
		cfqq = cfq_get_queue(cfqd, is_sync, cic->ioc, gfp_mask);

		if (!cfqq)
			goto queue_fail;

		cic_set_cfqq(cic, cfqq, is_sync);
	}

	cfqq->allocated[rw]++;
	cfq_clear_cfqq_must_alloc(cfqq);
	atomic_inc(&cfqq->ref);

	spin_unlock_irqrestore(q->queue_lock, flags);

	rq->elevator_private = cic;
	rq->elevator_private2 = cfqq;
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
	unsigned long flags;

	spin_lock_irqsave(q->queue_lock, flags);
	blk_start_queueing(q);
	spin_unlock_irqrestore(q->queue_lock, flags);
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
		if (!RB_EMPTY_ROOT(&cfqq->sort_list)) {
			cfq_mark_cfqq_must_dispatch(cfqq);
			goto out_kick;
		}
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

	spin_unlock_irq(q->queue_lock);

	cfq_shutdown_timer_wq(cfqd);

	kfree(cfqd);
}

static void *cfq_init_queue(struct request_queue *q)
{
	struct cfq_data *cfqd;

	cfqd = kmalloc_node(sizeof(*cfqd), GFP_KERNEL | __GFP_ZERO, q->node);
	if (!cfqd)
		return NULL;

	cfqd->service_tree = CFQ_RB_ROOT;
	INIT_LIST_HEAD(&cfqd->cic_list);

	cfqd->queue = q;

	init_timer(&cfqd->idle_slice_timer);
	cfqd->idle_slice_timer.function = cfq_idle_slice_timer;
	cfqd->idle_slice_timer.data = (unsigned long) cfqd;

	INIT_WORK(&cfqd->unplug_work, cfq_kick_queue);

	cfqd->last_end_request = jiffies;
	cfqd->cfq_quantum = cfq_quantum;
	cfqd->cfq_fifo_expire[0] = cfq_fifo_expire[0];
	cfqd->cfq_fifo_expire[1] = cfq_fifo_expire[1];
	cfqd->cfq_back_max = cfq_back_max;
	cfqd->cfq_back_penalty = cfq_back_penalty;
	cfqd->cfq_slice[0] = cfq_slice_async;
	cfqd->cfq_slice[1] = cfq_slice_sync;
	cfqd->cfq_slice_async_rq = cfq_slice_async_rq;
	cfqd->cfq_slice_idle = cfq_slice_idle;
	cfqd->hw_tag = 1;

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
	__ATTR_NULL
};

static struct elevator_type iosched_cfq = {
	.ops = {
		.elevator_merge_fn = 		cfq_merge,
		.elevator_merged_fn =		cfq_merged_request,
		.elevator_merge_req_fn =	cfq_merged_requests,
		.elevator_allow_merge_fn =	cfq_allow_merge,
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

	return 0;
}

static void __exit cfq_exit(void)
{
	DECLARE_COMPLETION_ONSTACK(all_gone);
	elv_unregister(&iosched_cfq);
	ioc_gone = &all_gone;
	/* ioc_gone's update must be visible before reading ioc_count */
	smp_wmb();

	/*
	 * this also protects us from entering cfq_slab_kill() with
	 * pending RCU callbacks
	 */
	if (elv_ioc_count_read(ioc_count))
		wait_for_completion(&all_gone);
	cfq_slab_kill();
}

module_init(cfq_init);
module_exit(cfq_exit);

MODULE_AUTHOR("Jens Axboe");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Completely Fair Queueing IO scheduler");
