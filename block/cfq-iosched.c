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

/*
 * tunables
 */
static const int cfq_quantum = 4;		/* max queue in one round of service */
static const int cfq_fifo_expire[2] = { HZ / 4, HZ / 8 };
static const int cfq_back_max = 16 * 1024;	/* maximum backwards seek, in KiB */
static const int cfq_back_penalty = 2;		/* penalty of a backwards seek */

static const int cfq_slice_sync = HZ / 10;
static int cfq_slice_async = HZ / 25;
static const int cfq_slice_async_rq = 2;
static int cfq_slice_idle = HZ / 125;

/*
 * grace period before allowing idle class to get disk access
 */
#define CFQ_IDLE_GRACE		(HZ / 10)

/*
 * below this threshold, we consider thinktime immediate
 */
#define CFQ_MIN_TT		(2)

#define CFQ_SLICE_SCALE		(5)

#define RQ_CIC(rq)		((struct cfq_io_context*)(rq)->elevator_private)
#define RQ_CFQQ(rq)		((rq)->elevator_private2)

static struct kmem_cache *cfq_pool;
static struct kmem_cache *cfq_ioc_pool;

static DEFINE_PER_CPU(unsigned long, ioc_count);
static struct completion *ioc_gone;

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
	request_queue_t *queue;

	/*
	 * rr list of queues with requests and the count of them
	 */
	struct cfq_rb_root service_tree;
	unsigned int busy_queues;

	int rq_in_driver;
	int sync_flight;
	int hw_tag;

	/*
	 * idle window management
	 */
	struct timer_list idle_slice_timer;
	struct work_struct unplug_work;

	struct cfq_queue *active_queue;
	struct cfq_io_context *active_cic;

	struct cfq_queue *async_cfqq[IOPRIO_BE_NR];

	struct timer_list idle_class_timer;

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

	sector_t new_seek_mean;
	u64 new_seek_total;
};

/*
 * Per process-grouping structure
 */
struct cfq_queue {
	/* reference count */
	atomic_t ref;
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
	/* pending metadata requests */
	int meta_pending;
	/* fifo list of requests in sort_list */
	struct list_head fifo;

	unsigned long slice_end;
	long slice_resid;

	/* number of requests that are on the dispatch list or inside driver */
	int dispatched;

	/* io prio of this group */
	unsigned short ioprio, org_ioprio;
	unsigned short ioprio_class, org_ioprio_class;

	/* various state flags, see below */
	unsigned int flags;

	sector_t last_request_pos;
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
	cfqq->flags |= (1 << CFQ_CFQQ_FLAG_##name);			\
}									\
static inline void cfq_clear_cfqq_##name(struct cfq_queue *cfqq)	\
{									\
	cfqq->flags &= ~(1 << CFQ_CFQQ_FLAG_##name);			\
}									\
static inline int cfq_cfqq_##name(const struct cfq_queue *cfqq)		\
{									\
	return (cfqq->flags & (1 << CFQ_CFQQ_FLAG_##name)) != 0;	\
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

static void cfq_dispatch_insert(request_queue_t *, struct request *);
static struct cfq_queue *cfq_get_queue(struct cfq_data *, int,
				       struct task_struct *, gfp_t);
static struct cfq_io_context *cfq_cic_rb_lookup(struct cfq_data *,
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
	if (cfqd->busy_queues)
		kblockd_schedule_work(&cfqd->unplug_work);
}

static int cfq_queue_empty(request_queue_t *q)
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
static struct rb_node *cfq_rb_first(struct cfq_rb_root *root)
{
	if (!root->left)
		root->left = rb_first(&root->rb);

	return root->left;
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
	struct rb_node **p = &cfqd->service_tree.rb.rb_node;
	struct rb_node *parent = NULL;
	unsigned long rb_key;
	int left;

	if (!add_front) {
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
	while (*p) {
		struct cfq_queue *__cfqq;
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
static inline void
cfq_add_cfqq_rr(struct cfq_data *cfqd, struct cfq_queue *cfqq)
{
	BUG_ON(cfq_cfqq_on_rr(cfqq));
	cfq_mark_cfqq_on_rr(cfqq);
	cfqd->busy_queues++;

	cfq_resort_rr_list(cfqd, cfqq);
}

/*
 * Called when the cfqq no longer has requests pending, remove it from
 * the service tree.
 */
static inline void
cfq_del_cfqq_rr(struct cfq_data *cfqd, struct cfq_queue *cfqq)
{
	BUG_ON(!cfq_cfqq_on_rr(cfqq));
	cfq_clear_cfqq_on_rr(cfqq);

	if (!RB_EMPTY_NODE(&cfqq->rb_node))
		cfq_rb_erase(&cfqq->rb_node, &cfqd->service_tree);

	BUG_ON(!cfqd->busy_queues);
	cfqd->busy_queues--;
}

/*
 * rb tree support functions
 */
static inline void cfq_del_rq_rb(struct request *rq)
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

static inline void
cfq_reposition_rq_rb(struct cfq_queue *cfqq, struct request *rq)
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

	cic = cfq_cic_rb_lookup(cfqd, tsk->io_context);
	if (!cic)
		return NULL;

	cfqq = cic_to_cfqq(cic, cfq_bio_sync(bio));
	if (cfqq) {
		sector_t sector = bio->bi_sector + bio_sectors(bio);

		return elv_rb_find(&cfqq->sort_list, sector);
	}

	return NULL;
}

static void cfq_activate_request(request_queue_t *q, struct request *rq)
{
	struct cfq_data *cfqd = q->elevator->elevator_data;

	cfqd->rq_in_driver++;

	/*
	 * If the depth is larger 1, it really could be queueing. But lets
	 * make the mark a little higher - idling could still be good for
	 * low queueing, and a low queueing number could also just indicate
	 * a SCSI mid layer like behaviour where limit+1 is often seen.
	 */
	if (!cfqd->hw_tag && cfqd->rq_in_driver > 4)
		cfqd->hw_tag = 1;

	cfqd->last_position = rq->hard_sector + rq->hard_nr_sectors;
}

static void cfq_deactivate_request(request_queue_t *q, struct request *rq)
{
	struct cfq_data *cfqd = q->elevator->elevator_data;

	WARN_ON(!cfqd->rq_in_driver);
	cfqd->rq_in_driver--;
}

static void cfq_remove_request(struct request *rq)
{
	struct cfq_queue *cfqq = RQ_CFQQ(rq);

	if (cfqq->next_rq == rq)
		cfqq->next_rq = cfq_find_next_rq(cfqq->cfqd, cfqq, rq);

	list_del_init(&rq->queuelist);
	cfq_del_rq_rb(rq);

	if (rq_is_meta(rq)) {
		WARN_ON(!cfqq->meta_pending);
		cfqq->meta_pending--;
	}
}

static int cfq_merge(request_queue_t *q, struct request **req, struct bio *bio)
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

static void cfq_merged_request(request_queue_t *q, struct request *req,
			       int type)
{
	if (type == ELEVATOR_FRONT_MERGE) {
		struct cfq_queue *cfqq = RQ_CFQQ(req);

		cfq_reposition_rq_rb(cfqq, req);
	}
}

static void
cfq_merged_requests(request_queue_t *q, struct request *rq,
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

static int cfq_allow_merge(request_queue_t *q, struct request *rq,
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
	cic = cfq_cic_rb_lookup(cfqd, current->io_context);
	if (!cic)
		return 0;

	cfqq = cic_to_cfqq(cic, cfq_bio_sync(bio));
	if (cfqq == RQ_CFQQ(rq))
		return 1;

	return 0;
}

static inline void
__cfq_set_active_queue(struct cfq_data *cfqd, struct cfq_queue *cfqq)
{
	if (cfqq) {
		/*
		 * stop potential idle class queues waiting service
		 */
		del_timer(&cfqd->idle_class_timer);

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
	if (cfq_cfqq_wait_request(cfqq))
		del_timer(&cfqd->idle_slice_timer);

	cfq_clear_cfqq_must_dispatch(cfqq);
	cfq_clear_cfqq_wait_request(cfqq);

	/*
	 * store what was left of this slice, if the queue idled/timed out
	 */
	if (timed_out && !cfq_cfqq_slice_new(cfqq))
		cfqq->slice_resid = cfqq->slice_end - jiffies;

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
	struct cfq_queue *cfqq;
	struct rb_node *n;

	if (RB_EMPTY_ROOT(&cfqd->service_tree.rb))
		return NULL;

	n = cfq_rb_first(&cfqd->service_tree);
	cfqq = rb_entry(n, struct cfq_queue, rb_node);

	if (cfq_class_idle(cfqq)) {
		unsigned long end;

		/*
		 * if we have idle queues and no rt or be queues had
		 * pending requests, either allow immediate service if
		 * the grace period has passed or arm the idle grace
		 * timer
		 */
		end = cfqd->last_end_request + CFQ_IDLE_GRACE;
		if (time_before(jiffies, end)) {
			mod_timer(&cfqd->idle_class_timer, end);
			cfqq = NULL;
		}
	}

	return cfqq;
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

	WARN_ON(!RB_EMPTY_ROOT(&cfqq->sort_list));
	WARN_ON(cfq_cfqq_slice_new(cfqq));

	/*
	 * idle is disabled, either manually or by past process history
	 */
	if (!cfqd->cfq_slice_idle || !cfq_cfqq_idle_window(cfqq))
		return;

	/*
	 * task has exited, don't wait
	 */
	cic = cfqd->active_cic;
	if (!cic || !cic->ioc->task)
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
}

/*
 * Move request from internal lists to the request queue dispatch list.
 */
static void cfq_dispatch_insert(request_queue_t *q, struct request *rq)
{
	struct cfq_data *cfqd = q->elevator->elevator_data;
	struct cfq_queue *cfqq = RQ_CFQQ(rq);

	cfq_remove_request(rq);
	cfqq->dispatched++;
	elv_dispatch_sort(q, rq);

	if (cfq_cfqq_sync(cfqq))
		cfqd->sync_flight++;
}

/*
 * return expired entry, or NULL to just start from scratch in rbtree
 */
static inline struct request *cfq_check_fifo(struct cfq_queue *cfqq)
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
		return NULL;

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
		if ((rq = cfq_check_fifo(cfqq)) == NULL)
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

static inline int __cfq_forced_dispatch_cfqq(struct cfq_queue *cfqq)
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
	int dispatched = 0;
	struct rb_node *n;

	while ((n = cfq_rb_first(&cfqd->service_tree)) != NULL) {
		struct cfq_queue *cfqq = rb_entry(n, struct cfq_queue, rb_node);

		dispatched += __cfq_forced_dispatch_cfqq(cfqq);
	}

	cfq_slice_expired(cfqd, 0);

	BUG_ON(cfqd->busy_queues);

	return dispatched;
}

static int cfq_dispatch_requests(request_queue_t *q, int force)
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

		if (cfqq->dispatched >= max_dispatch) {
			if (cfqd->busy_queues > 1)
				break;
			if (cfqq->dispatched >= 4 * max_dispatch)
				break;
		}

		if (cfqd->sync_flight && !cfq_cfqq_sync(cfqq))
			break;

		cfq_clear_cfqq_must_dispatch(cfqq);
		cfq_clear_cfqq_wait_request(cfqq);
		del_timer(&cfqd->idle_slice_timer);

		dispatched += __cfq_dispatch_requests(cfqd, cfqq, max_dispatch);
	}

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

	BUG_ON(rb_first(&cfqq->sort_list));
	BUG_ON(cfqq->allocated[READ] + cfqq->allocated[WRITE]);
	BUG_ON(cfq_cfqq_on_rr(cfqq));

	if (unlikely(cfqd->active_queue == cfqq)) {
		__cfq_slice_expired(cfqd, cfqq, 0);
		cfq_schedule_dispatch(cfqd);
	}

	kmem_cache_free(cfq_pool, cfqq);
}

static void cfq_free_io_context(struct io_context *ioc)
{
	struct cfq_io_context *__cic;
	struct rb_node *n;
	int freed = 0;

	ioc->ioc_data = NULL;

	while ((n = rb_first(&ioc->cic_root)) != NULL) {
		__cic = rb_entry(n, struct cfq_io_context, rb_node);
		rb_erase(&__cic->rb_node, &ioc->cic_root);
		kmem_cache_free(cfq_ioc_pool, __cic);
		freed++;
	}

	elv_ioc_count_mod(ioc_count, -freed);

	if (ioc_gone && !elv_ioc_count_read(ioc_count))
		complete(ioc_gone);
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
	list_del_init(&cic->queue_list);
	smp_wmb();
	cic->key = NULL;

	if (cic->cfqq[ASYNC]) {
		cfq_exit_cfqq(cfqd, cic->cfqq[ASYNC]);
		cic->cfqq[ASYNC] = NULL;
	}

	if (cic->cfqq[SYNC]) {
		cfq_exit_cfqq(cfqd, cic->cfqq[SYNC]);
		cic->cfqq[SYNC] = NULL;
	}
}

static void cfq_exit_single_io_context(struct cfq_io_context *cic)
{
	struct cfq_data *cfqd = cic->key;

	if (cfqd) {
		request_queue_t *q = cfqd->queue;

		spin_lock_irq(q->queue_lock);
		__cfq_exit_single_io_context(cfqd, cic);
		spin_unlock_irq(q->queue_lock);
	}
}

/*
 * The process that ioc belongs to has exited, we need to clean up
 * and put the internal structures we have that belongs to that process.
 */
static void cfq_exit_io_context(struct io_context *ioc)
{
	struct cfq_io_context *__cic;
	struct rb_node *n;

	ioc->ioc_data = NULL;

	/*
	 * put the reference this task is holding to the various queues
	 */
	n = rb_first(&ioc->cic_root);
	while (n != NULL) {
		__cic = rb_entry(n, struct cfq_io_context, rb_node);

		cfq_exit_single_io_context(__cic);
		n = rb_next(n);
	}
}

static struct cfq_io_context *
cfq_alloc_io_context(struct cfq_data *cfqd, gfp_t gfp_mask)
{
	struct cfq_io_context *cic;

	cic = kmem_cache_alloc_node(cfq_ioc_pool, gfp_mask, cfqd->queue->node);
	if (cic) {
		memset(cic, 0, sizeof(*cic));
		cic->last_end_request = jiffies;
		INIT_LIST_HEAD(&cic->queue_list);
		cic->dtor = cfq_free_io_context;
		cic->exit = cfq_exit_io_context;
		elv_ioc_count_inc(ioc_count);
	}

	return cic;
}

static void cfq_init_prio_data(struct cfq_queue *cfqq)
{
	struct task_struct *tsk = current;
	int ioprio_class;

	if (!cfq_cfqq_prio_changed(cfqq))
		return;

	ioprio_class = IOPRIO_PRIO_CLASS(tsk->ioprio);
	switch (ioprio_class) {
		default:
			printk(KERN_ERR "cfq: bad prio %x\n", ioprio_class);
		case IOPRIO_CLASS_NONE:
			/*
			 * no prio set, place us in the middle of the BE classes
			 */
			cfqq->ioprio = task_nice_ioprio(tsk);
			cfqq->ioprio_class = IOPRIO_CLASS_BE;
			break;
		case IOPRIO_CLASS_RT:
			cfqq->ioprio = task_ioprio(tsk);
			cfqq->ioprio_class = IOPRIO_CLASS_RT;
			break;
		case IOPRIO_CLASS_BE:
			cfqq->ioprio = task_ioprio(tsk);
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

static inline void changed_ioprio(struct cfq_io_context *cic)
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
		new_cfqq = cfq_get_queue(cfqd, ASYNC, cic->ioc->task,
					 GFP_ATOMIC);
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
	struct cfq_io_context *cic;
	struct rb_node *n;

	ioc->ioprio_changed = 0;

	n = rb_first(&ioc->cic_root);
	while (n != NULL) {
		cic = rb_entry(n, struct cfq_io_context, rb_node);

		changed_ioprio(cic);
		n = rb_next(n);
	}
}

static struct cfq_queue *
cfq_find_alloc_queue(struct cfq_data *cfqd, int is_sync,
		     struct task_struct *tsk, gfp_t gfp_mask)
{
	struct cfq_queue *cfqq, *new_cfqq = NULL;
	struct cfq_io_context *cic;

retry:
	cic = cfq_cic_rb_lookup(cfqd, tsk->io_context);
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
			new_cfqq = kmem_cache_alloc_node(cfq_pool, gfp_mask|__GFP_NOFAIL, cfqd->queue->node);
			spin_lock_irq(cfqd->queue->queue_lock);
			goto retry;
		} else {
			cfqq = kmem_cache_alloc_node(cfq_pool, gfp_mask, cfqd->queue->node);
			if (!cfqq)
				goto out;
		}

		memset(cfqq, 0, sizeof(*cfqq));

		RB_CLEAR_NODE(&cfqq->rb_node);
		INIT_LIST_HEAD(&cfqq->fifo);

		atomic_set(&cfqq->ref, 0);
		cfqq->cfqd = cfqd;

		if (is_sync) {
			cfq_mark_cfqq_idle_window(cfqq);
			cfq_mark_cfqq_sync(cfqq);
		}

		cfq_mark_cfqq_prio_changed(cfqq);
		cfq_mark_cfqq_queue_new(cfqq);

		cfq_init_prio_data(cfqq);
	}

	if (new_cfqq)
		kmem_cache_free(cfq_pool, new_cfqq);

out:
	WARN_ON((gfp_mask & __GFP_WAIT) && !cfqq);
	return cfqq;
}

static struct cfq_queue *
cfq_get_queue(struct cfq_data *cfqd, int is_sync, struct task_struct *tsk,
	      gfp_t gfp_mask)
{
	const int ioprio = task_ioprio(tsk);
	struct cfq_queue *cfqq = NULL;

	if (!is_sync)
		cfqq = cfqd->async_cfqq[ioprio];
	if (!cfqq)
		cfqq = cfq_find_alloc_queue(cfqd, is_sync, tsk, gfp_mask);

	/*
	 * pin the queue now that it's allocated, scheduler exit will prune it
	 */
	if (!is_sync && !cfqd->async_cfqq[ioprio]) {
		atomic_inc(&cfqq->ref);
		cfqd->async_cfqq[ioprio] = cfqq;
	}

	atomic_inc(&cfqq->ref);
	return cfqq;
}

/*
 * We drop cfq io contexts lazily, so we may find a dead one.
 */
static void
cfq_drop_dead_cic(struct io_context *ioc, struct cfq_io_context *cic)
{
	WARN_ON(!list_empty(&cic->queue_list));

	if (ioc->ioc_data == cic)
		ioc->ioc_data = NULL;

	rb_erase(&cic->rb_node, &ioc->cic_root);
	kmem_cache_free(cfq_ioc_pool, cic);
	elv_ioc_count_dec(ioc_count);
}

static struct cfq_io_context *
cfq_cic_rb_lookup(struct cfq_data *cfqd, struct io_context *ioc)
{
	struct rb_node *n;
	struct cfq_io_context *cic;
	void *k, *key = cfqd;

	if (unlikely(!ioc))
		return NULL;

	/*
	 * we maintain a last-hit cache, to avoid browsing over the tree
	 */
	cic = ioc->ioc_data;
	if (cic && cic->key == cfqd)
		return cic;

restart:
	n = ioc->cic_root.rb_node;
	while (n) {
		cic = rb_entry(n, struct cfq_io_context, rb_node);
		/* ->key must be copied to avoid race with cfq_exit_queue() */
		k = cic->key;
		if (unlikely(!k)) {
			cfq_drop_dead_cic(ioc, cic);
			goto restart;
		}

		if (key < k)
			n = n->rb_left;
		else if (key > k)
			n = n->rb_right;
		else {
			ioc->ioc_data = cic;
			return cic;
		}
	}

	return NULL;
}

static inline void
cfq_cic_link(struct cfq_data *cfqd, struct io_context *ioc,
	     struct cfq_io_context *cic)
{
	struct rb_node **p;
	struct rb_node *parent;
	struct cfq_io_context *__cic;
	unsigned long flags;
	void *k;

	cic->ioc = ioc;
	cic->key = cfqd;

restart:
	parent = NULL;
	p = &ioc->cic_root.rb_node;
	while (*p) {
		parent = *p;
		__cic = rb_entry(parent, struct cfq_io_context, rb_node);
		/* ->key must be copied to avoid race with cfq_exit_queue() */
		k = __cic->key;
		if (unlikely(!k)) {
			cfq_drop_dead_cic(ioc, __cic);
			goto restart;
		}

		if (cic->key < k)
			p = &(*p)->rb_left;
		else if (cic->key > k)
			p = &(*p)->rb_right;
		else
			BUG();
	}

	rb_link_node(&cic->rb_node, parent, p);
	rb_insert_color(&cic->rb_node, &ioc->cic_root);

	spin_lock_irqsave(cfqd->queue->queue_lock, flags);
	list_add(&cic->queue_list, &cfqd->cic_list);
	spin_unlock_irqrestore(cfqd->queue->queue_lock, flags);
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

	cic = cfq_cic_rb_lookup(cfqd, ioc);
	if (cic)
		goto out;

	cic = cfq_alloc_io_context(cfqd, gfp_mask);
	if (cic == NULL)
		goto err;

	cfq_cic_link(cfqd, ioc, cic);
out:
	smp_read_barrier_depends();
	if (unlikely(ioc->ioprio_changed))
		cfq_ioc_set_ioprio(ioc);

	return cic;
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

	if (!cic->seek_samples) {
		cfqd->new_seek_total = (7*cic->seek_total + (u64)256*sdist) / 8;
		cfqd->new_seek_mean = cfqd->new_seek_total / 256;
	}

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
	int enable_idle;

	if (!cfq_cfqq_sync(cfqq))
		return;

	enable_idle = cfq_cfqq_idle_window(cfqq);

	if (!cic->ioc->task || !cfqd->cfq_slice_idle ||
	    (cfqd->hw_tag && CIC_SEEKY(cic)))
		enable_idle = 0;
	else if (sample_valid(cic->ttime_samples)) {
		if (cic->ttime_mean > cfqd->cfq_slice_idle)
			enable_idle = 0;
		else
			enable_idle = 1;
	}

	if (enable_idle)
		cfq_mark_cfqq_idle_window(cfqq);
	else
		cfq_clear_cfqq_idle_window(cfqq);
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

	if (rq_is_meta(rq))
		cfqq->meta_pending++;

	cfq_update_io_thinktime(cfqd, cic);
	cfq_update_io_seektime(cfqd, cic, rq);
	cfq_update_idle_window(cfqd, cfqq, cic);

	cic->last_request_pos = rq->sector + rq->nr_sectors;
	cfqq->last_request_pos = cic->last_request_pos;

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
		 * has some old slice time left and is of higher priority
		 */
		cfq_preempt_queue(cfqd, cfqq);
		cfq_mark_cfqq_must_dispatch(cfqq);
		blk_start_queueing(cfqd->queue);
	}
}

static void cfq_insert_request(request_queue_t *q, struct request *rq)
{
	struct cfq_data *cfqd = q->elevator->elevator_data;
	struct cfq_queue *cfqq = RQ_CFQQ(rq);

	cfq_init_prio_data(cfqq);

	cfq_add_rq_rb(rq);

	list_add_tail(&rq->queuelist, &cfqq->fifo);

	cfq_rq_enqueued(cfqd, cfqq, rq);
}

static void cfq_completed_request(request_queue_t *q, struct request *rq)
{
	struct cfq_queue *cfqq = RQ_CFQQ(rq);
	struct cfq_data *cfqd = cfqq->cfqd;
	const int sync = rq_is_sync(rq);
	unsigned long now;

	now = jiffies;

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
		if (cfq_slice_used(cfqq))
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

static int cfq_may_queue(request_queue_t *q, int rw)
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
	cic = cfq_cic_rb_lookup(cfqd, tsk->io_context);
	if (!cic)
		return ELV_MQUEUE_MAY;

	cfqq = cic_to_cfqq(cic, rw & REQ_RW_SYNC);
	if (cfqq) {
		cfq_init_prio_data(cfqq);
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
cfq_set_request(request_queue_t *q, struct request *rq, gfp_t gfp_mask)
{
	struct cfq_data *cfqd = q->elevator->elevator_data;
	struct task_struct *tsk = current;
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
		cfqq = cfq_get_queue(cfqd, is_sync, tsk, gfp_mask);

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
	return 1;
}

static void cfq_kick_queue(struct work_struct *work)
{
	struct cfq_data *cfqd =
		container_of(work, struct cfq_data, unplug_work);
	request_queue_t *q = cfqd->queue;
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

	spin_lock_irqsave(cfqd->queue->queue_lock, flags);

	if ((cfqq = cfqd->active_queue) != NULL) {
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

/*
 * Timer running if an idle class queue is waiting for service
 */
static void cfq_idle_class_timer(unsigned long data)
{
	struct cfq_data *cfqd = (struct cfq_data *) data;
	unsigned long flags, end;

	spin_lock_irqsave(cfqd->queue->queue_lock, flags);

	/*
	 * race with a non-idle queue, reset timer
	 */
	end = cfqd->last_end_request + CFQ_IDLE_GRACE;
	if (!time_after_eq(jiffies, end))
		mod_timer(&cfqd->idle_class_timer, end);
	else
		cfq_schedule_dispatch(cfqd);

	spin_unlock_irqrestore(cfqd->queue->queue_lock, flags);
}

static void cfq_shutdown_timer_wq(struct cfq_data *cfqd)
{
	del_timer_sync(&cfqd->idle_slice_timer);
	del_timer_sync(&cfqd->idle_class_timer);
	blk_sync_queue(cfqd->queue);
}

static void cfq_exit_queue(elevator_t *e)
{
	struct cfq_data *cfqd = e->elevator_data;
	request_queue_t *q = cfqd->queue;
	int i;

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

	/*
	 * Put the async queues
	 */
	for (i = 0; i < IOPRIO_BE_NR; i++)
		if (cfqd->async_cfqq[i])	
			cfq_put_queue(cfqd->async_cfqq[i]);

	spin_unlock_irq(q->queue_lock);

	cfq_shutdown_timer_wq(cfqd);

	kfree(cfqd);
}

static void *cfq_init_queue(request_queue_t *q)
{
	struct cfq_data *cfqd;

	cfqd = kmalloc_node(sizeof(*cfqd), GFP_KERNEL, q->node);
	if (!cfqd)
		return NULL;

	memset(cfqd, 0, sizeof(*cfqd));

	cfqd->service_tree = CFQ_RB_ROOT;
	INIT_LIST_HEAD(&cfqd->cic_list);

	cfqd->queue = q;

	init_timer(&cfqd->idle_slice_timer);
	cfqd->idle_slice_timer.function = cfq_idle_slice_timer;
	cfqd->idle_slice_timer.data = (unsigned long) cfqd;

	init_timer(&cfqd->idle_class_timer);
	cfqd->idle_class_timer.function = cfq_idle_class_timer;
	cfqd->idle_class_timer.data = (unsigned long) cfqd;

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

	return cfqd;
}

static void cfq_slab_kill(void)
{
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
static ssize_t __FUNC(elevator_t *e, char *page)			\
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
static ssize_t __FUNC(elevator_t *e, const char *page, size_t count)	\
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
STORE_FUNCTION(cfq_fifo_expire_sync_store, &cfqd->cfq_fifo_expire[1], 1, UINT_MAX, 1);
STORE_FUNCTION(cfq_fifo_expire_async_store, &cfqd->cfq_fifo_expire[0], 1, UINT_MAX, 1);
STORE_FUNCTION(cfq_back_seek_max_store, &cfqd->cfq_back_max, 0, UINT_MAX, 0);
STORE_FUNCTION(cfq_back_seek_penalty_store, &cfqd->cfq_back_penalty, 1, UINT_MAX, 0);
STORE_FUNCTION(cfq_slice_idle_store, &cfqd->cfq_slice_idle, 0, UINT_MAX, 1);
STORE_FUNCTION(cfq_slice_sync_store, &cfqd->cfq_slice[1], 1, UINT_MAX, 1);
STORE_FUNCTION(cfq_slice_async_store, &cfqd->cfq_slice[0], 1, UINT_MAX, 1);
STORE_FUNCTION(cfq_slice_async_rq_store, &cfqd->cfq_slice_async_rq, 1, UINT_MAX, 0);
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
	int ret;

	/*
	 * could be 0 on HZ < 1000 setups
	 */
	if (!cfq_slice_async)
		cfq_slice_async = 1;
	if (!cfq_slice_idle)
		cfq_slice_idle = 1;

	if (cfq_slab_setup())
		return -ENOMEM;

	ret = elv_register(&iosched_cfq);
	if (ret)
		cfq_slab_kill();

	return ret;
}

static void __exit cfq_exit(void)
{
	DECLARE_COMPLETION_ONSTACK(all_gone);
	elv_unregister(&iosched_cfq);
	ioc_gone = &all_gone;
	/* ioc_gone's update must be visible before reading ioc_count */
	smp_wmb();
	if (elv_ioc_count_read(ioc_count))
		wait_for_completion(ioc_gone);
	synchronize_rcu();
	cfq_slab_kill();
}

module_init(cfq_init);
module_exit(cfq_exit);

MODULE_AUTHOR("Jens Axboe");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Completely Fair Queueing IO scheduler");
