/*
 *  linux/drivers/block/cfq-iosched.c
 *
 *  CFQ, or complete fairness queueing, disk scheduler.
 *
 *  Based on ideas from a previously unfinished io
 *  scheduler (round robin per-process disk scheduling) and Andrea Arcangeli.
 *
 *  Copyright (C) 2003 Jens Axboe <axboe@suse.de>
 */
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/blkdev.h>
#include <linux/elevator.h>
#include <linux/bio.h>
#include <linux/config.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/compiler.h>
#include <linux/hash.h>
#include <linux/rbtree.h>
#include <linux/mempool.h>

static unsigned long max_elapsed_crq;
static unsigned long max_elapsed_dispatch;

/*
 * tunables
 */
static int cfq_quantum = 4;		/* max queue in one round of service */
static int cfq_queued = 8;		/* minimum rq allocate limit per-queue*/
static int cfq_service = HZ;		/* period over which service is avg */
static int cfq_fifo_expire_r = HZ / 2;	/* fifo timeout for sync requests */
static int cfq_fifo_expire_w = 5 * HZ;	/* fifo timeout for async requests */
static int cfq_fifo_rate = HZ / 8;	/* fifo expiry rate */
static int cfq_back_max = 16 * 1024;	/* maximum backwards seek, in KiB */
static int cfq_back_penalty = 2;	/* penalty of a backwards seek */

/*
 * for the hash of cfqq inside the cfqd
 */
#define CFQ_QHASH_SHIFT		6
#define CFQ_QHASH_ENTRIES	(1 << CFQ_QHASH_SHIFT)
#define list_entry_qhash(entry)	hlist_entry((entry), struct cfq_queue, cfq_hash)

/*
 * for the hash of crq inside the cfqq
 */
#define CFQ_MHASH_SHIFT		6
#define CFQ_MHASH_BLOCK(sec)	((sec) >> 3)
#define CFQ_MHASH_ENTRIES	(1 << CFQ_MHASH_SHIFT)
#define CFQ_MHASH_FN(sec)	hash_long(CFQ_MHASH_BLOCK(sec), CFQ_MHASH_SHIFT)
#define rq_hash_key(rq)		((rq)->sector + (rq)->nr_sectors)
#define list_entry_hash(ptr)	hlist_entry((ptr), struct cfq_rq, hash)

#define list_entry_cfqq(ptr)	list_entry((ptr), struct cfq_queue, cfq_list)

#define RQ_DATA(rq)		(rq)->elevator_private

/*
 * rb-tree defines
 */
#define RB_NONE			(2)
#define RB_EMPTY(node)		((node)->rb_node == NULL)
#define RB_CLEAR_COLOR(node)	(node)->rb_color = RB_NONE
#define RB_CLEAR(node)		do {	\
	(node)->rb_parent = NULL;	\
	RB_CLEAR_COLOR((node));		\
	(node)->rb_right = NULL;	\
	(node)->rb_left = NULL;		\
} while (0)
#define RB_CLEAR_ROOT(root)	((root)->rb_node = NULL)
#define ON_RB(node)		((node)->rb_color != RB_NONE)
#define rb_entry_crq(node)	rb_entry((node), struct cfq_rq, rb_node)
#define rq_rb_key(rq)		(rq)->sector

/*
 * threshold for switching off non-tag accounting
 */
#define CFQ_MAX_TAG		(4)

/*
 * sort key types and names
 */
enum {
	CFQ_KEY_PGID,
	CFQ_KEY_TGID,
	CFQ_KEY_UID,
	CFQ_KEY_GID,
	CFQ_KEY_LAST,
};

static char *cfq_key_types[] = { "pgid", "tgid", "uid", "gid", NULL };

static kmem_cache_t *crq_pool;
static kmem_cache_t *cfq_pool;
static kmem_cache_t *cfq_ioc_pool;

struct cfq_data {
	struct list_head rr_list;
	struct list_head empty_list;

	struct hlist_head *cfq_hash;
	struct hlist_head *crq_hash;

	/* queues on rr_list (ie they have pending requests */
	unsigned int busy_queues;

	unsigned int max_queued;

	atomic_t ref;

	int key_type;

	mempool_t *crq_pool;

	request_queue_t *queue;

	sector_t last_sector;

	int rq_in_driver;

	/*
	 * tunables, see top of file
	 */
	unsigned int cfq_quantum;
	unsigned int cfq_queued;
	unsigned int cfq_fifo_expire_r;
	unsigned int cfq_fifo_expire_w;
	unsigned int cfq_fifo_batch_expire;
	unsigned int cfq_back_penalty;
	unsigned int cfq_back_max;
	unsigned int find_best_crq;

	unsigned int cfq_tagged;
};

struct cfq_queue {
	/* reference count */
	atomic_t ref;
	/* parent cfq_data */
	struct cfq_data *cfqd;
	/* hash of mergeable requests */
	struct hlist_node cfq_hash;
	/* hash key */
	unsigned long key;
	/* whether queue is on rr (or empty) list */
	int on_rr;
	/* on either rr or empty list of cfqd */
	struct list_head cfq_list;
	/* sorted list of pending requests */
	struct rb_root sort_list;
	/* if fifo isn't expired, next request to serve */
	struct cfq_rq *next_crq;
	/* requests queued in sort_list */
	int queued[2];
	/* currently allocated requests */
	int allocated[2];
	/* fifo list of requests in sort_list */
	struct list_head fifo[2];
	/* last time fifo expired */
	unsigned long last_fifo_expire;

	int key_type;

	unsigned long service_start;
	unsigned long service_used;

	unsigned int max_rate;

	/* number of requests that have been handed to the driver */
	int in_flight;
	/* number of currently allocated requests */
	int alloc_limit[2];
};

struct cfq_rq {
	struct rb_node rb_node;
	sector_t rb_key;
	struct request *request;
	struct hlist_node hash;

	struct cfq_queue *cfq_queue;
	struct cfq_io_context *io_context;

	unsigned long service_start;
	unsigned long queue_start;

	unsigned int in_flight : 1;
	unsigned int accounted : 1;
	unsigned int is_sync   : 1;
	unsigned int is_write  : 1;
};

static struct cfq_queue *cfq_find_cfq_hash(struct cfq_data *, unsigned long);
static void cfq_dispatch_sort(request_queue_t *, struct cfq_rq *);
static void cfq_update_next_crq(struct cfq_rq *);
static void cfq_put_cfqd(struct cfq_data *cfqd);

/*
 * what the fairness is based on (ie how processes are grouped and
 * differentiated)
 */
static inline unsigned long
cfq_hash_key(struct cfq_data *cfqd, struct task_struct *tsk)
{
	/*
	 * optimize this so that ->key_type is the offset into the struct
	 */
	switch (cfqd->key_type) {
		case CFQ_KEY_PGID:
			return process_group(tsk);
		default:
		case CFQ_KEY_TGID:
			return tsk->tgid;
		case CFQ_KEY_UID:
			return tsk->uid;
		case CFQ_KEY_GID:
			return tsk->gid;
	}
}

/*
 * lots of deadline iosched dupes, can be abstracted later...
 */
static inline void cfq_del_crq_hash(struct cfq_rq *crq)
{
	hlist_del_init(&crq->hash);
}

static void cfq_remove_merge_hints(request_queue_t *q, struct cfq_rq *crq)
{
	cfq_del_crq_hash(crq);

	if (q->last_merge == crq->request)
		q->last_merge = NULL;

	cfq_update_next_crq(crq);
}

static inline void cfq_add_crq_hash(struct cfq_data *cfqd, struct cfq_rq *crq)
{
	const int hash_idx = CFQ_MHASH_FN(rq_hash_key(crq->request));

	BUG_ON(!hlist_unhashed(&crq->hash));

	hlist_add_head(&crq->hash, &cfqd->crq_hash[hash_idx]);
}

static struct request *cfq_find_rq_hash(struct cfq_data *cfqd, sector_t offset)
{
	struct hlist_head *hash_list = &cfqd->crq_hash[CFQ_MHASH_FN(offset)];
	struct hlist_node *entry, *next;

	hlist_for_each_safe(entry, next, hash_list) {
		struct cfq_rq *crq = list_entry_hash(entry);
		struct request *__rq = crq->request;

		BUG_ON(hlist_unhashed(&crq->hash));

		if (!rq_mergeable(__rq)) {
			cfq_del_crq_hash(crq);
			continue;
		}

		if (rq_hash_key(__rq) == offset)
			return __rq;
	}

	return NULL;
}

/*
 * Lifted from AS - choose which of crq1 and crq2 that is best served now.
 * We choose the request that is closest to the head right now. Distance
 * behind the head are penalized and only allowed to a certain extent.
 */
static struct cfq_rq *
cfq_choose_req(struct cfq_data *cfqd, struct cfq_rq *crq1, struct cfq_rq *crq2)
{
	sector_t last, s1, s2, d1 = 0, d2 = 0;
	int r1_wrap = 0, r2_wrap = 0;	/* requests are behind the disk head */
	unsigned long back_max;

	if (crq1 == NULL || crq1 == crq2)
		return crq2;
	if (crq2 == NULL)
		return crq1;

	s1 = crq1->request->sector;
	s2 = crq2->request->sector;

	last = cfqd->last_sector;

#if 0
	if (!list_empty(&cfqd->queue->queue_head)) {
		struct list_head *entry = &cfqd->queue->queue_head;
		unsigned long distance = ~0UL;
		struct request *rq;

		while ((entry = entry->prev) != &cfqd->queue->queue_head) {
			rq = list_entry_rq(entry);

			if (blk_barrier_rq(rq))
				break;

			if (distance < abs(s1 - rq->sector + rq->nr_sectors)) {
				distance = abs(s1 - rq->sector +rq->nr_sectors);
				last = rq->sector + rq->nr_sectors;
			}
			if (distance < abs(s2 - rq->sector + rq->nr_sectors)) {
				distance = abs(s2 - rq->sector +rq->nr_sectors);
				last = rq->sector + rq->nr_sectors;
			}
		}
	}
#endif

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
		r1_wrap = 1;

	if (s2 >= last)
		d2 = s2 - last;
	else if (s2 + back_max >= last)
		d2 = (last - s2) * cfqd->cfq_back_penalty;
	else
		r2_wrap = 1;

	/* Found required data */
	if (!r1_wrap && r2_wrap)
		return crq1;
	else if (!r2_wrap && r1_wrap)
		return crq2;
	else if (r1_wrap && r2_wrap) {
		/* both behind the head */
		if (s1 <= s2)
			return crq1;
		else
			return crq2;
	}

	/* Both requests in front of the head */
	if (d1 < d2)
		return crq1;
	else if (d2 < d1)
		return crq2;
	else {
		if (s1 >= s2)
			return crq1;
		else
			return crq2;
	}
}

/*
 * would be nice to take fifo expire time into account as well
 */
static struct cfq_rq *
cfq_find_next_crq(struct cfq_data *cfqd, struct cfq_queue *cfqq,
		  struct cfq_rq *last)
{
	struct cfq_rq *crq_next = NULL, *crq_prev = NULL;
	struct rb_node *rbnext, *rbprev;

	if (!ON_RB(&last->rb_node))
		return NULL;

	if ((rbnext = rb_next(&last->rb_node)) == NULL)
		rbnext = rb_first(&cfqq->sort_list);

	rbprev = rb_prev(&last->rb_node);

	if (rbprev)
		crq_prev = rb_entry_crq(rbprev);
	if (rbnext)
		crq_next = rb_entry_crq(rbnext);

	return cfq_choose_req(cfqd, crq_next, crq_prev);
}

static void cfq_update_next_crq(struct cfq_rq *crq)
{
	struct cfq_queue *cfqq = crq->cfq_queue;

	if (cfqq->next_crq == crq)
		cfqq->next_crq = cfq_find_next_crq(cfqq->cfqd, cfqq, crq);
}

static int cfq_check_sort_rr_list(struct cfq_queue *cfqq)
{
	struct list_head *head = &cfqq->cfqd->rr_list;
	struct list_head *next, *prev;

	/*
	 * list might still be ordered
	 */
	next = cfqq->cfq_list.next;
	if (next != head) {
		struct cfq_queue *cnext = list_entry_cfqq(next);

		if (cfqq->service_used > cnext->service_used)
			return 1;
	}

	prev = cfqq->cfq_list.prev;
	if (prev != head) {
		struct cfq_queue *cprev = list_entry_cfqq(prev);

		if (cfqq->service_used < cprev->service_used)
			return 1;
	}

	return 0;
}

static void cfq_sort_rr_list(struct cfq_queue *cfqq, int new_queue)
{
	struct list_head *entry = &cfqq->cfqd->rr_list;

	if (!cfqq->on_rr)
		return;
	if (!new_queue && !cfq_check_sort_rr_list(cfqq))
		return;

	list_del(&cfqq->cfq_list);

	/*
	 * sort by our mean service_used, sub-sort by in-flight requests
	 */
	while ((entry = entry->prev) != &cfqq->cfqd->rr_list) {
		struct cfq_queue *__cfqq = list_entry_cfqq(entry);

		if (cfqq->service_used > __cfqq->service_used)
			break;
		else if (cfqq->service_used == __cfqq->service_used) {
			struct list_head *prv;

			while ((prv = entry->prev) != &cfqq->cfqd->rr_list) {
				__cfqq = list_entry_cfqq(prv);

				WARN_ON(__cfqq->service_used > cfqq->service_used);
				if (cfqq->service_used != __cfqq->service_used)
					break;
				if (cfqq->in_flight > __cfqq->in_flight)
					break;

				entry = prv;
			}
		}
	}

	list_add(&cfqq->cfq_list, entry);
}

/*
 * add to busy list of queues for service, trying to be fair in ordering
 * the pending list according to requests serviced
 */
static inline void
cfq_add_cfqq_rr(struct cfq_data *cfqd, struct cfq_queue *cfqq)
{
	/*
	 * it's currently on the empty list
	 */
	cfqq->on_rr = 1;
	cfqd->busy_queues++;

	if (time_after(jiffies, cfqq->service_start + cfq_service))
		cfqq->service_used >>= 3;

	cfq_sort_rr_list(cfqq, 1);
}

static inline void
cfq_del_cfqq_rr(struct cfq_data *cfqd, struct cfq_queue *cfqq)
{
	list_move(&cfqq->cfq_list, &cfqd->empty_list);
	cfqq->on_rr = 0;

	BUG_ON(!cfqd->busy_queues);
	cfqd->busy_queues--;
}

/*
 * rb tree support functions
 */
static inline void cfq_del_crq_rb(struct cfq_rq *crq)
{
	struct cfq_queue *cfqq = crq->cfq_queue;

	if (ON_RB(&crq->rb_node)) {
		struct cfq_data *cfqd = cfqq->cfqd;

		BUG_ON(!cfqq->queued[crq->is_sync]);

		cfq_update_next_crq(crq);

		cfqq->queued[crq->is_sync]--;
		rb_erase(&crq->rb_node, &cfqq->sort_list);
		RB_CLEAR_COLOR(&crq->rb_node);

		if (RB_EMPTY(&cfqq->sort_list) && cfqq->on_rr)
			cfq_del_cfqq_rr(cfqd, cfqq);
	}
}

static struct cfq_rq *
__cfq_add_crq_rb(struct cfq_rq *crq)
{
	struct rb_node **p = &crq->cfq_queue->sort_list.rb_node;
	struct rb_node *parent = NULL;
	struct cfq_rq *__crq;

	while (*p) {
		parent = *p;
		__crq = rb_entry_crq(parent);

		if (crq->rb_key < __crq->rb_key)
			p = &(*p)->rb_left;
		else if (crq->rb_key > __crq->rb_key)
			p = &(*p)->rb_right;
		else
			return __crq;
	}

	rb_link_node(&crq->rb_node, parent, p);
	return NULL;
}

static void cfq_add_crq_rb(struct cfq_rq *crq)
{
	struct cfq_queue *cfqq = crq->cfq_queue;
	struct cfq_data *cfqd = cfqq->cfqd;
	struct request *rq = crq->request;
	struct cfq_rq *__alias;

	crq->rb_key = rq_rb_key(rq);
	cfqq->queued[crq->is_sync]++;

	/*
	 * looks a little odd, but the first insert might return an alias.
	 * if that happens, put the alias on the dispatch list
	 */
	while ((__alias = __cfq_add_crq_rb(crq)) != NULL)
		cfq_dispatch_sort(cfqd->queue, __alias);

	rb_insert_color(&crq->rb_node, &cfqq->sort_list);

	if (!cfqq->on_rr)
		cfq_add_cfqq_rr(cfqd, cfqq);

	/*
	 * check if this request is a better next-serve candidate
	 */
	cfqq->next_crq = cfq_choose_req(cfqd, cfqq->next_crq, crq);
}

static inline void
cfq_reposition_crq_rb(struct cfq_queue *cfqq, struct cfq_rq *crq)
{
	if (ON_RB(&crq->rb_node)) {
		rb_erase(&crq->rb_node, &cfqq->sort_list);
		cfqq->queued[crq->is_sync]--;
	}

	cfq_add_crq_rb(crq);
}

static struct request *
cfq_find_rq_rb(struct cfq_data *cfqd, sector_t sector)
{
	const unsigned long key = cfq_hash_key(cfqd, current);
	struct cfq_queue *cfqq = cfq_find_cfq_hash(cfqd, key);
	struct rb_node *n;

	if (!cfqq)
		goto out;

	n = cfqq->sort_list.rb_node;
	while (n) {
		struct cfq_rq *crq = rb_entry_crq(n);

		if (sector < crq->rb_key)
			n = n->rb_left;
		else if (sector > crq->rb_key)
			n = n->rb_right;
		else
			return crq->request;
	}

out:
	return NULL;
}

static void cfq_deactivate_request(request_queue_t *q, struct request *rq)
{
	struct cfq_rq *crq = RQ_DATA(rq);

	if (crq) {
		struct cfq_queue *cfqq = crq->cfq_queue;

		if (cfqq->cfqd->cfq_tagged) {
			cfqq->service_used--;
			cfq_sort_rr_list(cfqq, 0);
		}

		if (crq->accounted) {
			crq->accounted = 0;
			cfqq->cfqd->rq_in_driver--;
		}
	}
}

/*
 * make sure the service time gets corrected on reissue of this request
 */
static void cfq_requeue_request(request_queue_t *q, struct request *rq)
{
	cfq_deactivate_request(q, rq);
	list_add(&rq->queuelist, &q->queue_head);
}

static void cfq_remove_request(request_queue_t *q, struct request *rq)
{
	struct cfq_rq *crq = RQ_DATA(rq);

	if (crq) {
		cfq_remove_merge_hints(q, crq);
		list_del_init(&rq->queuelist);

		if (crq->cfq_queue)
			cfq_del_crq_rb(crq);
	}
}

static int
cfq_merge(request_queue_t *q, struct request **req, struct bio *bio)
{
	struct cfq_data *cfqd = q->elevator->elevator_data;
	struct request *__rq;
	int ret;

	ret = elv_try_last_merge(q, bio);
	if (ret != ELEVATOR_NO_MERGE) {
		__rq = q->last_merge;
		goto out_insert;
	}

	__rq = cfq_find_rq_hash(cfqd, bio->bi_sector);
	if (__rq) {
		BUG_ON(__rq->sector + __rq->nr_sectors != bio->bi_sector);

		if (elv_rq_merge_ok(__rq, bio)) {
			ret = ELEVATOR_BACK_MERGE;
			goto out;
		}
	}

	__rq = cfq_find_rq_rb(cfqd, bio->bi_sector + bio_sectors(bio));
	if (__rq) {
		if (elv_rq_merge_ok(__rq, bio)) {
			ret = ELEVATOR_FRONT_MERGE;
			goto out;
		}
	}

	return ELEVATOR_NO_MERGE;
out:
	q->last_merge = __rq;
out_insert:
	*req = __rq;
	return ret;
}

static void cfq_merged_request(request_queue_t *q, struct request *req)
{
	struct cfq_data *cfqd = q->elevator->elevator_data;
	struct cfq_rq *crq = RQ_DATA(req);

	cfq_del_crq_hash(crq);
	cfq_add_crq_hash(cfqd, crq);

	if (ON_RB(&crq->rb_node) && (rq_rb_key(req) != crq->rb_key)) {
		struct cfq_queue *cfqq = crq->cfq_queue;

		cfq_update_next_crq(crq);
		cfq_reposition_crq_rb(cfqq, crq);
	}

	q->last_merge = req;
}

static void
cfq_merged_requests(request_queue_t *q, struct request *rq,
		    struct request *next)
{
	struct cfq_rq *crq = RQ_DATA(rq);
	struct cfq_rq *cnext = RQ_DATA(next);

	cfq_merged_request(q, rq);

	if (!list_empty(&rq->queuelist) && !list_empty(&next->queuelist)) {
		if (time_before(cnext->queue_start, crq->queue_start)) {
			list_move(&rq->queuelist, &next->queuelist);
			crq->queue_start = cnext->queue_start;
		}
	}

	cfq_update_next_crq(cnext);
	cfq_remove_request(q, next);
}

/*
 * we dispatch cfqd->cfq_quantum requests in total from the rr_list queues,
 * this function sector sorts the selected request to minimize seeks. we start
 * at cfqd->last_sector, not 0.
 */
static void cfq_dispatch_sort(request_queue_t *q, struct cfq_rq *crq)
{
	struct cfq_data *cfqd = q->elevator->elevator_data;
	struct cfq_queue *cfqq = crq->cfq_queue;
	struct list_head *head = &q->queue_head, *entry = head;
	struct request *__rq;
	sector_t last;

	cfq_del_crq_rb(crq);
	cfq_remove_merge_hints(q, crq);
	list_del(&crq->request->queuelist);

	last = cfqd->last_sector;
	while ((entry = entry->prev) != head) {
		__rq = list_entry_rq(entry);

		if (blk_barrier_rq(crq->request))
			break;
		if (!blk_fs_request(crq->request))
			break;

		if (crq->request->sector > __rq->sector)
			break;
		if (__rq->sector > last && crq->request->sector < last) {
			last = crq->request->sector;
			break;
		}
	}

	cfqd->last_sector = last;
	crq->in_flight = 1;
	cfqq->in_flight++;
	list_add(&crq->request->queuelist, entry);
}

/*
 * return expired entry, or NULL to just start from scratch in rbtree
 */
static inline struct cfq_rq *cfq_check_fifo(struct cfq_queue *cfqq)
{
	struct cfq_data *cfqd = cfqq->cfqd;
	const int reads = !list_empty(&cfqq->fifo[0]);
	const int writes = !list_empty(&cfqq->fifo[1]);
	unsigned long now = jiffies;
	struct cfq_rq *crq;

	if (time_before(now, cfqq->last_fifo_expire + cfqd->cfq_fifo_batch_expire))
		return NULL;

	crq = RQ_DATA(list_entry(cfqq->fifo[0].next, struct request, queuelist));
	if (reads && time_after(now, crq->queue_start + cfqd->cfq_fifo_expire_r)) {
		cfqq->last_fifo_expire = now;
		return crq;
	}

	crq = RQ_DATA(list_entry(cfqq->fifo[1].next, struct request, queuelist));
	if (writes && time_after(now, crq->queue_start + cfqd->cfq_fifo_expire_w)) {
		cfqq->last_fifo_expire = now;
		return crq;
	}

	return NULL;
}

/*
 * dispatch a single request from given queue
 */
static inline void
cfq_dispatch_request(request_queue_t *q, struct cfq_data *cfqd,
		     struct cfq_queue *cfqq)
{
	struct cfq_rq *crq;

	/*
	 * follow expired path, else get first next available
	 */
	if ((crq = cfq_check_fifo(cfqq)) == NULL) {
		if (cfqd->find_best_crq)
			crq = cfqq->next_crq;
		else
			crq = rb_entry_crq(rb_first(&cfqq->sort_list));
	}

	cfqd->last_sector = crq->request->sector + crq->request->nr_sectors;

	/*
	 * finally, insert request into driver list
	 */
	cfq_dispatch_sort(q, crq);
}

static int cfq_dispatch_requests(request_queue_t *q, int max_dispatch)
{
	struct cfq_data *cfqd = q->elevator->elevator_data;
	struct cfq_queue *cfqq;
	struct list_head *entry, *tmp;
	int queued, busy_queues, first_round;

	if (list_empty(&cfqd->rr_list))
		return 0;

	queued = 0;
	first_round = 1;
restart:
	busy_queues = 0;
	list_for_each_safe(entry, tmp, &cfqd->rr_list) {
		cfqq = list_entry_cfqq(entry);

		BUG_ON(RB_EMPTY(&cfqq->sort_list));

		/*
		 * first round of queueing, only select from queues that
		 * don't already have io in-flight
		 */
		if (first_round && cfqq->in_flight)
			continue;

		cfq_dispatch_request(q, cfqd, cfqq);

		if (!RB_EMPTY(&cfqq->sort_list))
			busy_queues++;

		queued++;
	}

	if ((queued < max_dispatch) && (busy_queues || first_round)) {
		first_round = 0;
		goto restart;
	}

	return queued;
}

static inline void cfq_account_dispatch(struct cfq_rq *crq)
{
	struct cfq_queue *cfqq = crq->cfq_queue;
	struct cfq_data *cfqd = cfqq->cfqd;
	unsigned long now, elapsed;

	if (!blk_fs_request(crq->request))
		return;

	/*
	 * accounted bit is necessary since some drivers will call
	 * elv_next_request() many times for the same request (eg ide)
	 */
	if (crq->accounted)
		return;

	now = jiffies;
	if (cfqq->service_start == ~0UL)
		cfqq->service_start = now;

	/*
	 * on drives with tagged command queueing, command turn-around time
	 * doesn't necessarily reflect the time spent processing this very
	 * command inside the drive. so do the accounting differently there,
	 * by just sorting on the number of requests
	 */
	if (cfqd->cfq_tagged) {
		if (time_after(now, cfqq->service_start + cfq_service)) {
			cfqq->service_start = now;
			cfqq->service_used /= 10;
		}

		cfqq->service_used++;
		cfq_sort_rr_list(cfqq, 0);
	}

	elapsed = now - crq->queue_start;
	if (elapsed > max_elapsed_dispatch)
		max_elapsed_dispatch = elapsed;

	crq->accounted = 1;
	crq->service_start = now;

	if (++cfqd->rq_in_driver >= CFQ_MAX_TAG && !cfqd->cfq_tagged) {
		cfqq->cfqd->cfq_tagged = 1;
		printk("cfq: depth %d reached, tagging now on\n", CFQ_MAX_TAG);
	}
}

static inline void
cfq_account_completion(struct cfq_queue *cfqq, struct cfq_rq *crq)
{
	struct cfq_data *cfqd = cfqq->cfqd;

	if (!crq->accounted)
		return;

	WARN_ON(!cfqd->rq_in_driver);
	cfqd->rq_in_driver--;

	if (!cfqd->cfq_tagged) {
		unsigned long now = jiffies;
		unsigned long duration = now - crq->service_start;

		if (time_after(now, cfqq->service_start + cfq_service)) {
			cfqq->service_start = now;
			cfqq->service_used >>= 3;
		}

		cfqq->service_used += duration;
		cfq_sort_rr_list(cfqq, 0);

		if (duration > max_elapsed_crq)
			max_elapsed_crq = duration;
	}
}

static struct request *cfq_next_request(request_queue_t *q)
{
	struct cfq_data *cfqd = q->elevator->elevator_data;
	struct request *rq;

	if (!list_empty(&q->queue_head)) {
		struct cfq_rq *crq;
dispatch:
		rq = list_entry_rq(q->queue_head.next);

		if ((crq = RQ_DATA(rq)) != NULL) {
			cfq_remove_merge_hints(q, crq);
			cfq_account_dispatch(crq);
		}

		return rq;
	}

	if (cfq_dispatch_requests(q, cfqd->cfq_quantum))
		goto dispatch;

	return NULL;
}

/*
 * task holds one reference to the queue, dropped when task exits. each crq
 * in-flight on this queue also holds a reference, dropped when crq is freed.
 *
 * queue lock must be held here.
 */
static void cfq_put_queue(struct cfq_queue *cfqq)
{
	BUG_ON(!atomic_read(&cfqq->ref));

	if (!atomic_dec_and_test(&cfqq->ref))
		return;

	BUG_ON(rb_first(&cfqq->sort_list));
	BUG_ON(cfqq->on_rr);

	cfq_put_cfqd(cfqq->cfqd);

	/*
	 * it's on the empty list and still hashed
	 */
	list_del(&cfqq->cfq_list);
	hlist_del(&cfqq->cfq_hash);
	kmem_cache_free(cfq_pool, cfqq);
}

static inline struct cfq_queue *
__cfq_find_cfq_hash(struct cfq_data *cfqd, unsigned long key, const int hashval)
{
	struct hlist_head *hash_list = &cfqd->cfq_hash[hashval];
	struct hlist_node *entry, *next;

	hlist_for_each_safe(entry, next, hash_list) {
		struct cfq_queue *__cfqq = list_entry_qhash(entry);

		if (__cfqq->key == key)
			return __cfqq;
	}

	return NULL;
}

static struct cfq_queue *
cfq_find_cfq_hash(struct cfq_data *cfqd, unsigned long key)
{
	return __cfq_find_cfq_hash(cfqd, key, hash_long(key, CFQ_QHASH_SHIFT));
}

static inline void
cfq_rehash_cfqq(struct cfq_data *cfqd, struct cfq_queue **cfqq,
		struct cfq_io_context *cic)
{
	unsigned long hashkey = cfq_hash_key(cfqd, current);
	unsigned long hashval = hash_long(hashkey, CFQ_QHASH_SHIFT);
	struct cfq_queue *__cfqq;
	unsigned long flags;

	spin_lock_irqsave(cfqd->queue->queue_lock, flags);

	hlist_del(&(*cfqq)->cfq_hash);

	__cfqq = __cfq_find_cfq_hash(cfqd, hashkey, hashval);
	if (!__cfqq || __cfqq == *cfqq) {
		__cfqq = *cfqq;
		hlist_add_head(&__cfqq->cfq_hash, &cfqd->cfq_hash[hashval]);
		__cfqq->key_type = cfqd->key_type;
	} else {
		atomic_inc(&__cfqq->ref);
		cic->cfqq = __cfqq;
		cfq_put_queue(*cfqq);
		*cfqq = __cfqq;
	}

	cic->cfqq = __cfqq;
	spin_unlock_irqrestore(cfqd->queue->queue_lock, flags);
}

static void cfq_free_io_context(struct cfq_io_context *cic)
{
	kmem_cache_free(cfq_ioc_pool, cic);
}

/*
 * locking hierarchy is: io_context lock -> queue locks
 */
static void cfq_exit_io_context(struct cfq_io_context *cic)
{
	struct cfq_queue *cfqq = cic->cfqq;
	struct list_head *entry = &cic->list;
	request_queue_t *q;
	unsigned long flags;

	/*
	 * put the reference this task is holding to the various queues
	 */
	spin_lock_irqsave(&cic->ioc->lock, flags);
	while ((entry = cic->list.next) != &cic->list) {
		struct cfq_io_context *__cic;

		__cic = list_entry(entry, struct cfq_io_context, list);
		list_del(entry);

		q = __cic->cfqq->cfqd->queue;
		spin_lock(q->queue_lock);
		cfq_put_queue(__cic->cfqq);
		spin_unlock(q->queue_lock);
	}

	q = cfqq->cfqd->queue;
	spin_lock(q->queue_lock);
	cfq_put_queue(cfqq);
	spin_unlock(q->queue_lock);

	cic->cfqq = NULL;
	spin_unlock_irqrestore(&cic->ioc->lock, flags);
}

static struct cfq_io_context *cfq_alloc_io_context(int gfp_flags)
{
	struct cfq_io_context *cic = kmem_cache_alloc(cfq_ioc_pool, gfp_flags);

	if (cic) {
		cic->dtor = cfq_free_io_context;
		cic->exit = cfq_exit_io_context;
		INIT_LIST_HEAD(&cic->list);
		cic->cfqq = NULL;
	}

	return cic;
}

/*
 * Setup general io context and cfq io context. There can be several cfq
 * io contexts per general io context, if this process is doing io to more
 * than one device managed by cfq. Note that caller is holding a reference to
 * cfqq, so we don't need to worry about it disappearing
 */
static struct cfq_io_context *
cfq_get_io_context(struct cfq_queue **cfqq, int gfp_flags)
{
	struct cfq_data *cfqd = (*cfqq)->cfqd;
	struct cfq_queue *__cfqq = *cfqq;
	struct cfq_io_context *cic;
	struct io_context *ioc;

	might_sleep_if(gfp_flags & __GFP_WAIT);

	ioc = get_io_context(gfp_flags);
	if (!ioc)
		return NULL;

	if ((cic = ioc->cic) == NULL) {
		cic = cfq_alloc_io_context(gfp_flags);

		if (cic == NULL)
			goto err;

		ioc->cic = cic;
		cic->ioc = ioc;
		cic->cfqq = __cfqq;
		atomic_inc(&__cfqq->ref);
	} else {
		struct cfq_io_context *__cic;
		unsigned long flags;

		/*
		 * since the first cic on the list is actually the head
		 * itself, need to check this here or we'll duplicate an
		 * cic per ioc for no reason
		 */
		if (cic->cfqq == __cfqq)
			goto out;

		/*
		 * cic exists, check if we already are there. linear search
		 * should be ok here, the list will usually not be more than
		 * 1 or a few entries long
		 */
		spin_lock_irqsave(&ioc->lock, flags);
		list_for_each_entry(__cic, &cic->list, list) {
			/*
			 * this process is already holding a reference to
			 * this queue, so no need to get one more
			 */
			if (__cic->cfqq == __cfqq) {
				cic = __cic;
				spin_unlock_irqrestore(&ioc->lock, flags);
				goto out;
			}
		}
		spin_unlock_irqrestore(&ioc->lock, flags);

		/*
		 * nope, process doesn't have a cic assoicated with this
		 * cfqq yet. get a new one and add to list
		 */
		__cic = cfq_alloc_io_context(gfp_flags);
		if (__cic == NULL)
			goto err;

		__cic->ioc = ioc;
		__cic->cfqq = __cfqq;
		atomic_inc(&__cfqq->ref);
		spin_lock_irqsave(&ioc->lock, flags);
		list_add(&__cic->list, &cic->list);
		spin_unlock_irqrestore(&ioc->lock, flags);

		cic = __cic;
		*cfqq = __cfqq;
	}

out:
	/*
	 * if key_type has been changed on the fly, we lazily rehash
	 * each queue at lookup time
	 */
	if ((*cfqq)->key_type != cfqd->key_type)
		cfq_rehash_cfqq(cfqd, cfqq, cic);

	return cic;
err:
	put_io_context(ioc);
	return NULL;
}

static struct cfq_queue *
__cfq_get_queue(struct cfq_data *cfqd, unsigned long key, int gfp_mask)
{
	const int hashval = hash_long(key, CFQ_QHASH_SHIFT);
	struct cfq_queue *cfqq, *new_cfqq = NULL;

retry:
	cfqq = __cfq_find_cfq_hash(cfqd, key, hashval);

	if (!cfqq) {
		if (new_cfqq) {
			cfqq = new_cfqq;
			new_cfqq = NULL;
		} else {
			spin_unlock_irq(cfqd->queue->queue_lock);
			new_cfqq = kmem_cache_alloc(cfq_pool, gfp_mask);
			spin_lock_irq(cfqd->queue->queue_lock);

			if (!new_cfqq && !(gfp_mask & __GFP_WAIT))
				goto out;

			goto retry;
		}

		memset(cfqq, 0, sizeof(*cfqq));

		INIT_HLIST_NODE(&cfqq->cfq_hash);
		INIT_LIST_HEAD(&cfqq->cfq_list);
		RB_CLEAR_ROOT(&cfqq->sort_list);
		INIT_LIST_HEAD(&cfqq->fifo[0]);
		INIT_LIST_HEAD(&cfqq->fifo[1]);

		cfqq->key = key;
		hlist_add_head(&cfqq->cfq_hash, &cfqd->cfq_hash[hashval]);
		atomic_set(&cfqq->ref, 0);
		cfqq->cfqd = cfqd;
		atomic_inc(&cfqd->ref);
		cfqq->key_type = cfqd->key_type;
		cfqq->service_start = ~0UL;
	}

	if (new_cfqq)
		kmem_cache_free(cfq_pool, new_cfqq);

	atomic_inc(&cfqq->ref);
out:
	WARN_ON((gfp_mask & __GFP_WAIT) && !cfqq);
	return cfqq;
}

static void cfq_enqueue(struct cfq_data *cfqd, struct cfq_rq *crq)
{
	crq->is_sync = 0;
	if (rq_data_dir(crq->request) == READ || current->flags & PF_SYNCWRITE)
		crq->is_sync = 1;

	cfq_add_crq_rb(crq);
	crq->queue_start = jiffies;

	list_add_tail(&crq->request->queuelist, &crq->cfq_queue->fifo[crq->is_sync]);
}

static void
cfq_insert_request(request_queue_t *q, struct request *rq, int where)
{
	struct cfq_data *cfqd = q->elevator->elevator_data;
	struct cfq_rq *crq = RQ_DATA(rq);

	switch (where) {
		case ELEVATOR_INSERT_BACK:
			while (cfq_dispatch_requests(q, cfqd->cfq_quantum))
				;
			list_add_tail(&rq->queuelist, &q->queue_head);
			break;
		case ELEVATOR_INSERT_FRONT:
			list_add(&rq->queuelist, &q->queue_head);
			break;
		case ELEVATOR_INSERT_SORT:
			BUG_ON(!blk_fs_request(rq));
			cfq_enqueue(cfqd, crq);
			break;
		default:
			printk("%s: bad insert point %d\n", __FUNCTION__,where);
			return;
	}

	if (rq_mergeable(rq)) {
		cfq_add_crq_hash(cfqd, crq);

		if (!q->last_merge)
			q->last_merge = rq;
	}
}

static int cfq_queue_empty(request_queue_t *q)
{
	struct cfq_data *cfqd = q->elevator->elevator_data;

	return list_empty(&q->queue_head) && list_empty(&cfqd->rr_list);
}

static void cfq_completed_request(request_queue_t *q, struct request *rq)
{
	struct cfq_rq *crq = RQ_DATA(rq);
	struct cfq_queue *cfqq;

	if (unlikely(!blk_fs_request(rq)))
		return;

	cfqq = crq->cfq_queue;

	if (crq->in_flight) {
		WARN_ON(!cfqq->in_flight);
		cfqq->in_flight--;
	}

	cfq_account_completion(cfqq, crq);
}

static struct request *
cfq_former_request(request_queue_t *q, struct request *rq)
{
	struct cfq_rq *crq = RQ_DATA(rq);
	struct rb_node *rbprev = rb_prev(&crq->rb_node);

	if (rbprev)
		return rb_entry_crq(rbprev)->request;

	return NULL;
}

static struct request *
cfq_latter_request(request_queue_t *q, struct request *rq)
{
	struct cfq_rq *crq = RQ_DATA(rq);
	struct rb_node *rbnext = rb_next(&crq->rb_node);

	if (rbnext)
		return rb_entry_crq(rbnext)->request;

	return NULL;
}

static int cfq_may_queue(request_queue_t *q, int rw)
{
	struct cfq_data *cfqd = q->elevator->elevator_data;
	struct cfq_queue *cfqq;
	int ret = ELV_MQUEUE_MAY;

	if (current->flags & PF_MEMALLOC)
		return ELV_MQUEUE_MAY;

	cfqq = cfq_find_cfq_hash(cfqd, cfq_hash_key(cfqd, current));
	if (cfqq) {
		int limit = cfqd->max_queued;

		if (cfqq->allocated[rw] < cfqd->cfq_queued)
			return ELV_MQUEUE_MUST;

		if (cfqd->busy_queues)
			limit = q->nr_requests / cfqd->busy_queues;

		if (limit < cfqd->cfq_queued)
			limit = cfqd->cfq_queued;
		else if (limit > cfqd->max_queued)
			limit = cfqd->max_queued;

		if (cfqq->allocated[rw] >= limit) {
			if (limit > cfqq->alloc_limit[rw])
				cfqq->alloc_limit[rw] = limit;

			ret = ELV_MQUEUE_NO;
		}
	}

	return ret;
}

static void cfq_check_waiters(request_queue_t *q, struct cfq_queue *cfqq)
{
	struct request_list *rl = &q->rq;
	const int write = waitqueue_active(&rl->wait[WRITE]);
	const int read = waitqueue_active(&rl->wait[READ]);

	if (read && cfqq->allocated[READ] < cfqq->alloc_limit[READ])
		wake_up(&rl->wait[READ]);
	if (write && cfqq->allocated[WRITE] < cfqq->alloc_limit[WRITE])
		wake_up(&rl->wait[WRITE]);
}

/*
 * queue lock held here
 */
static void cfq_put_request(request_queue_t *q, struct request *rq)
{
	struct cfq_data *cfqd = q->elevator->elevator_data;
	struct cfq_rq *crq = RQ_DATA(rq);

	if (crq) {
		struct cfq_queue *cfqq = crq->cfq_queue;

		BUG_ON(q->last_merge == rq);
		BUG_ON(!hlist_unhashed(&crq->hash));

		if (crq->io_context)
			put_io_context(crq->io_context->ioc);

		BUG_ON(!cfqq->allocated[crq->is_write]);
		cfqq->allocated[crq->is_write]--;

		mempool_free(crq, cfqd->crq_pool);
		rq->elevator_private = NULL;

		smp_mb();
		cfq_check_waiters(q, cfqq);
		cfq_put_queue(cfqq);
	}
}

/*
 * Allocate cfq data structures associated with this request. A queue and
 */
static int cfq_set_request(request_queue_t *q, struct request *rq, int gfp_mask)
{
	struct cfq_data *cfqd = q->elevator->elevator_data;
	struct cfq_io_context *cic;
	const int rw = rq_data_dir(rq);
	struct cfq_queue *cfqq, *saved_cfqq;
	struct cfq_rq *crq;
	unsigned long flags;

	might_sleep_if(gfp_mask & __GFP_WAIT);

	spin_lock_irqsave(q->queue_lock, flags);

	cfqq = __cfq_get_queue(cfqd, cfq_hash_key(cfqd, current), gfp_mask);
	if (!cfqq)
		goto out_lock;

repeat:
	if (cfqq->allocated[rw] >= cfqd->max_queued)
		goto out_lock;

	cfqq->allocated[rw]++;
	spin_unlock_irqrestore(q->queue_lock, flags);

	/*
	 * if hashing type has changed, the cfq_queue might change here.
	 */
	saved_cfqq = cfqq;
	cic = cfq_get_io_context(&cfqq, gfp_mask);
	if (!cic)
		goto err;

	/*
	 * repeat allocation checks on queue change
	 */
	if (unlikely(saved_cfqq != cfqq)) {
		spin_lock_irqsave(q->queue_lock, flags);
		saved_cfqq->allocated[rw]--;
		goto repeat;
	}

	crq = mempool_alloc(cfqd->crq_pool, gfp_mask);
	if (crq) {
		RB_CLEAR(&crq->rb_node);
		crq->rb_key = 0;
		crq->request = rq;
		INIT_HLIST_NODE(&crq->hash);
		crq->cfq_queue = cfqq;
		crq->io_context = cic;
		crq->service_start = crq->queue_start = 0;
		crq->in_flight = crq->accounted = crq->is_sync = 0;
		crq->is_write = rw;
		rq->elevator_private = crq;
		cfqq->alloc_limit[rw] = 0;
		return 0;
	}

	put_io_context(cic->ioc);
err:
	spin_lock_irqsave(q->queue_lock, flags);
	cfqq->allocated[rw]--;
	cfq_put_queue(cfqq);
out_lock:
	spin_unlock_irqrestore(q->queue_lock, flags);
	return 1;
}

static void cfq_put_cfqd(struct cfq_data *cfqd)
{
	request_queue_t *q = cfqd->queue;

	if (!atomic_dec_and_test(&cfqd->ref))
		return;

	blk_put_queue(q);

	mempool_destroy(cfqd->crq_pool);
	kfree(cfqd->crq_hash);
	kfree(cfqd->cfq_hash);
	kfree(cfqd);
}

static void cfq_exit_queue(elevator_t *e)
{
	cfq_put_cfqd(e->elevator_data);
}

static int cfq_init_queue(request_queue_t *q, elevator_t *e)
{
	struct cfq_data *cfqd;
	int i;

	cfqd = kmalloc(sizeof(*cfqd), GFP_KERNEL);
	if (!cfqd)
		return -ENOMEM;

	memset(cfqd, 0, sizeof(*cfqd));
	INIT_LIST_HEAD(&cfqd->rr_list);
	INIT_LIST_HEAD(&cfqd->empty_list);

	cfqd->crq_hash = kmalloc(sizeof(struct hlist_head) * CFQ_MHASH_ENTRIES, GFP_KERNEL);
	if (!cfqd->crq_hash)
		goto out_crqhash;

	cfqd->cfq_hash = kmalloc(sizeof(struct hlist_head) * CFQ_QHASH_ENTRIES, GFP_KERNEL);
	if (!cfqd->cfq_hash)
		goto out_cfqhash;

	cfqd->crq_pool = mempool_create(BLKDEV_MIN_RQ, mempool_alloc_slab, mempool_free_slab, crq_pool);
	if (!cfqd->crq_pool)
		goto out_crqpool;

	for (i = 0; i < CFQ_MHASH_ENTRIES; i++)
		INIT_HLIST_HEAD(&cfqd->crq_hash[i]);
	for (i = 0; i < CFQ_QHASH_ENTRIES; i++)
		INIT_HLIST_HEAD(&cfqd->cfq_hash[i]);

	e->elevator_data = cfqd;

	cfqd->queue = q;
	atomic_inc(&q->refcnt);

	/*
	 * just set it to some high value, we want anyone to be able to queue
	 * some requests. fairness is handled differently
	 */
	q->nr_requests = 1024;
	cfqd->max_queued = q->nr_requests / 16;
	q->nr_batching = cfq_queued;
	cfqd->key_type = CFQ_KEY_TGID;
	cfqd->find_best_crq = 1;
	atomic_set(&cfqd->ref, 1);

	cfqd->cfq_queued = cfq_queued;
	cfqd->cfq_quantum = cfq_quantum;
	cfqd->cfq_fifo_expire_r = cfq_fifo_expire_r;
	cfqd->cfq_fifo_expire_w = cfq_fifo_expire_w;
	cfqd->cfq_fifo_batch_expire = cfq_fifo_rate;
	cfqd->cfq_back_max = cfq_back_max;
	cfqd->cfq_back_penalty = cfq_back_penalty;

	return 0;
out_crqpool:
	kfree(cfqd->cfq_hash);
out_cfqhash:
	kfree(cfqd->crq_hash);
out_crqhash:
	kfree(cfqd);
	return -ENOMEM;
}

static void cfq_slab_kill(void)
{
	if (crq_pool)
		kmem_cache_destroy(crq_pool);
	if (cfq_pool)
		kmem_cache_destroy(cfq_pool);
	if (cfq_ioc_pool)
		kmem_cache_destroy(cfq_ioc_pool);
}

static int __init cfq_slab_setup(void)
{
	crq_pool = kmem_cache_create("crq_pool", sizeof(struct cfq_rq), 0, 0,
					NULL, NULL);
	if (!crq_pool)
		goto fail;

	cfq_pool = kmem_cache_create("cfq_pool", sizeof(struct cfq_queue), 0, 0,
					NULL, NULL);
	if (!cfq_pool)
		goto fail;

	cfq_ioc_pool = kmem_cache_create("cfq_ioc_pool",
			sizeof(struct cfq_io_context), 0, 0, NULL, NULL);
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
struct cfq_fs_entry {
	struct attribute attr;
	ssize_t (*show)(struct cfq_data *, char *);
	ssize_t (*store)(struct cfq_data *, const char *, size_t);
};

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

static ssize_t
cfq_clear_elapsed(struct cfq_data *cfqd, const char *page, size_t count)
{
	max_elapsed_dispatch = max_elapsed_crq = 0;
	return count;
}

static ssize_t
cfq_set_key_type(struct cfq_data *cfqd, const char *page, size_t count)
{
	spin_lock_irq(cfqd->queue->queue_lock);
	if (!strncmp(page, "pgid", 4))
		cfqd->key_type = CFQ_KEY_PGID;
	else if (!strncmp(page, "tgid", 4))
		cfqd->key_type = CFQ_KEY_TGID;
	else if (!strncmp(page, "uid", 3))
		cfqd->key_type = CFQ_KEY_UID;
	else if (!strncmp(page, "gid", 3))
		cfqd->key_type = CFQ_KEY_GID;
	spin_unlock_irq(cfqd->queue->queue_lock);
	return count;
}

static ssize_t
cfq_read_key_type(struct cfq_data *cfqd, char *page)
{
	ssize_t len = 0;
	int i;

	for (i = CFQ_KEY_PGID; i < CFQ_KEY_LAST; i++) {
		if (cfqd->key_type == i)
			len += sprintf(page+len, "[%s] ", cfq_key_types[i]);
		else
			len += sprintf(page+len, "%s ", cfq_key_types[i]);
	}
	len += sprintf(page+len, "\n");
	return len;
}

#define SHOW_FUNCTION(__FUNC, __VAR, __CONV)				\
static ssize_t __FUNC(struct cfq_data *cfqd, char *page)		\
{									\
	unsigned int __data = __VAR;					\
	if (__CONV)							\
		__data = jiffies_to_msecs(__data);			\
	return cfq_var_show(__data, (page));				\
}
SHOW_FUNCTION(cfq_quantum_show, cfqd->cfq_quantum, 0);
SHOW_FUNCTION(cfq_queued_show, cfqd->cfq_queued, 0);
SHOW_FUNCTION(cfq_fifo_expire_r_show, cfqd->cfq_fifo_expire_r, 1);
SHOW_FUNCTION(cfq_fifo_expire_w_show, cfqd->cfq_fifo_expire_w, 1);
SHOW_FUNCTION(cfq_fifo_batch_expire_show, cfqd->cfq_fifo_batch_expire, 1);
SHOW_FUNCTION(cfq_find_best_show, cfqd->find_best_crq, 0);
SHOW_FUNCTION(cfq_back_max_show, cfqd->cfq_back_max, 0);
SHOW_FUNCTION(cfq_back_penalty_show, cfqd->cfq_back_penalty, 0);
#undef SHOW_FUNCTION

#define STORE_FUNCTION(__FUNC, __PTR, MIN, MAX, __CONV)			\
static ssize_t __FUNC(struct cfq_data *cfqd, const char *page, size_t count)	\
{									\
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
STORE_FUNCTION(cfq_queued_store, &cfqd->cfq_queued, 1, UINT_MAX, 0);
STORE_FUNCTION(cfq_fifo_expire_r_store, &cfqd->cfq_fifo_expire_r, 1, UINT_MAX, 1);
STORE_FUNCTION(cfq_fifo_expire_w_store, &cfqd->cfq_fifo_expire_w, 1, UINT_MAX, 1);
STORE_FUNCTION(cfq_fifo_batch_expire_store, &cfqd->cfq_fifo_batch_expire, 0, UINT_MAX, 1);
STORE_FUNCTION(cfq_find_best_store, &cfqd->find_best_crq, 0, 1, 0);
STORE_FUNCTION(cfq_back_max_store, &cfqd->cfq_back_max, 0, UINT_MAX, 0);
STORE_FUNCTION(cfq_back_penalty_store, &cfqd->cfq_back_penalty, 1, UINT_MAX, 0);
#undef STORE_FUNCTION

static struct cfq_fs_entry cfq_quantum_entry = {
	.attr = {.name = "quantum", .mode = S_IRUGO | S_IWUSR },
	.show = cfq_quantum_show,
	.store = cfq_quantum_store,
};
static struct cfq_fs_entry cfq_queued_entry = {
	.attr = {.name = "queued", .mode = S_IRUGO | S_IWUSR },
	.show = cfq_queued_show,
	.store = cfq_queued_store,
};
static struct cfq_fs_entry cfq_fifo_expire_r_entry = {
	.attr = {.name = "fifo_expire_sync", .mode = S_IRUGO | S_IWUSR },
	.show = cfq_fifo_expire_r_show,
	.store = cfq_fifo_expire_r_store,
};
static struct cfq_fs_entry cfq_fifo_expire_w_entry = {
	.attr = {.name = "fifo_expire_async", .mode = S_IRUGO | S_IWUSR },
	.show = cfq_fifo_expire_w_show,
	.store = cfq_fifo_expire_w_store,
};
static struct cfq_fs_entry cfq_fifo_batch_expire_entry = {
	.attr = {.name = "fifo_batch_expire", .mode = S_IRUGO | S_IWUSR },
	.show = cfq_fifo_batch_expire_show,
	.store = cfq_fifo_batch_expire_store,
};
static struct cfq_fs_entry cfq_find_best_entry = {
	.attr = {.name = "find_best_crq", .mode = S_IRUGO | S_IWUSR },
	.show = cfq_find_best_show,
	.store = cfq_find_best_store,
};
static struct cfq_fs_entry cfq_back_max_entry = {
	.attr = {.name = "back_seek_max", .mode = S_IRUGO | S_IWUSR },
	.show = cfq_back_max_show,
	.store = cfq_back_max_store,
};
static struct cfq_fs_entry cfq_back_penalty_entry = {
	.attr = {.name = "back_seek_penalty", .mode = S_IRUGO | S_IWUSR },
	.show = cfq_back_penalty_show,
	.store = cfq_back_penalty_store,
};
static struct cfq_fs_entry cfq_clear_elapsed_entry = {
	.attr = {.name = "clear_elapsed", .mode = S_IWUSR },
	.store = cfq_clear_elapsed,
};
static struct cfq_fs_entry cfq_key_type_entry = {
	.attr = {.name = "key_type", .mode = S_IRUGO | S_IWUSR },
	.show = cfq_read_key_type,
	.store = cfq_set_key_type,
};

static struct attribute *default_attrs[] = {
	&cfq_quantum_entry.attr,
	&cfq_queued_entry.attr,
	&cfq_fifo_expire_r_entry.attr,
	&cfq_fifo_expire_w_entry.attr,
	&cfq_fifo_batch_expire_entry.attr,
	&cfq_key_type_entry.attr,
	&cfq_find_best_entry.attr,
	&cfq_back_max_entry.attr,
	&cfq_back_penalty_entry.attr,
	&cfq_clear_elapsed_entry.attr,
	NULL,
};

#define to_cfq(atr) container_of((atr), struct cfq_fs_entry, attr)

static ssize_t
cfq_attr_show(struct kobject *kobj, struct attribute *attr, char *page)
{
	elevator_t *e = container_of(kobj, elevator_t, kobj);
	struct cfq_fs_entry *entry = to_cfq(attr);

	if (!entry->show)
		return 0;

	return entry->show(e->elevator_data, page);
}

static ssize_t
cfq_attr_store(struct kobject *kobj, struct attribute *attr,
	       const char *page, size_t length)
{
	elevator_t *e = container_of(kobj, elevator_t, kobj);
	struct cfq_fs_entry *entry = to_cfq(attr);

	if (!entry->store)
		return -EINVAL;

	return entry->store(e->elevator_data, page, length);
}

static struct sysfs_ops cfq_sysfs_ops = {
	.show	= cfq_attr_show,
	.store	= cfq_attr_store,
};

static struct kobj_type cfq_ktype = {
	.sysfs_ops	= &cfq_sysfs_ops,
	.default_attrs	= default_attrs,
};

static struct elevator_type iosched_cfq = {
	.ops = {
		.elevator_merge_fn = 		cfq_merge,
		.elevator_merged_fn =		cfq_merged_request,
		.elevator_merge_req_fn =	cfq_merged_requests,
		.elevator_next_req_fn =		cfq_next_request,
		.elevator_add_req_fn =		cfq_insert_request,
		.elevator_remove_req_fn =	cfq_remove_request,
		.elevator_requeue_req_fn =	cfq_requeue_request,
		.elevator_deactivate_req_fn =	cfq_deactivate_request,
		.elevator_queue_empty_fn =	cfq_queue_empty,
		.elevator_completed_req_fn =	cfq_completed_request,
		.elevator_former_req_fn =	cfq_former_request,
		.elevator_latter_req_fn =	cfq_latter_request,
		.elevator_set_req_fn =		cfq_set_request,
		.elevator_put_req_fn =		cfq_put_request,
		.elevator_may_queue_fn =	cfq_may_queue,
		.elevator_init_fn =		cfq_init_queue,
		.elevator_exit_fn =		cfq_exit_queue,
	},
	.elevator_ktype =	&cfq_ktype,
	.elevator_name =	"cfq",
	.elevator_owner =	THIS_MODULE,
};

static int __init cfq_init(void)
{
	int ret;

	if (cfq_slab_setup())
		return -ENOMEM;

	ret = elv_register(&iosched_cfq);
	if (!ret) {
		__module_get(THIS_MODULE);
		return 0;
	}

	cfq_slab_kill();
	return ret;
}

static void __exit cfq_exit(void)
{
	cfq_slab_kill();
	elv_unregister(&iosched_cfq);
}

module_init(cfq_init);
module_exit(cfq_exit);

MODULE_AUTHOR("Jens Axboe");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Completely Fair Queueing IO scheduler");
