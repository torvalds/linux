/*
 * BFQ, or Budget Fair Queueing, disk scheduler.
 *
 * Based on ideas and code from CFQ:
 * Copyright (C) 2003 Jens Axboe <axboe@kernel.dk>
 *
 * Copyright (C) 2008 Fabio Checconi <fabio@gandalf.sssup.it>
 *		      Paolo Valente <paolo.valente@unimore.it>
 *
 * Licensed under the GPL-2 as detailed in the accompanying COPYING.BFQ file.
 *
 * BFQ is a proportional share disk scheduling algorithm based on the
 * slice-by-slice service scheme of CFQ. But BFQ assigns budgets,
 * measured in number of sectors, to tasks instead of time slices.
 * The disk is not granted to the active task for a given time slice,
 * but until it has exahusted its assigned budget.  This change from
 * the time to the service domain allows BFQ to distribute the disk
 * bandwidth among tasks as desired, without any distortion due to
 * ZBR, workload fluctuations or other factors. BFQ uses an ad hoc
 * internal scheduler, called B-WF2Q+, to schedule tasks according to
 * their budgets.  Thanks to this accurate scheduler, BFQ can afford
 * to assign high budgets to disk-bound non-seeky tasks (to boost the
 * throughput), and yet guarantee low latencies to interactive and
 * soft real-time applications.
 *
 * BFQ has been introduced in [1], where the interested reader can
 * find an accurate description of the algorithm, the bandwidth
 * distribution and latency guarantees it provides, plus formal proofs
 * of all the properties.  With respect to the algorithm presented in
 * the paper, this implementation adds several little heuristics, and
 * a hierarchical extension, based on H-WF2Q+.
 *
 * B-WF2Q+ is based on WF2Q+, that is described in [2], together with
 * H-WF2Q+, while the augmented tree used to implement B-WF2Q+ with O(log N)
 * complexity derives from the one introduced with EEVDF in [3].
 *
 * [1] P. Valente and F. Checconi, ``High Throughput Disk Scheduling
 *     with Deterministic Guarantees on Bandwidth Distribution,'',
 *     IEEE Transactions on Computer, May 2010.
 *
 *     http://algo.ing.unimo.it/people/paolo/disk_sched/bfq-techreport.pdf
 *
 * [2] Jon C.R. Bennett and H. Zhang, ``Hierarchical Packet Fair Queueing
 *     Algorithms,'' IEEE/ACM Transactions on Networking, 5(5):675-689,
 *     Oct 1997.
 *
 *     http://www.cs.cmu.edu/~hzhang/papers/TON-97-Oct.ps.gz
 *
 * [3] I. Stoica and H. Abdel-Wahab, ``Earliest Eligible Virtual Deadline
 *     First: A Flexible and Accurate Mechanism for Proportional Share
 *     Resource Allocation,'' technical report.
 *
 *     http://www.cs.berkeley.edu/~istoica/papers/eevdf-tr-95.pdf
 */
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/blkdev.h>
#include <linux/cgroup.h>
#include <linux/elevator.h>
#include <linux/jiffies.h>
#include <linux/rbtree.h>
#include <linux/ioprio.h>
#include "bfq.h"

/* Max number of dispatches in one round of service. */
static const int bfq_quantum = 16;

/* Expiration time of sync (0) and async (1) requests, in jiffies. */
static const int bfq_fifo_expire[2] = { HZ / 4, HZ / 8 };

/* Maximum backwards seek, in KiB. */
static const int bfq_back_max = 16 * 1024;

/* Penalty of a backwards seek, in number of sectors. */
static const int bfq_back_penalty = 1;

/* Idling period duration, in jiffies. */
static int bfq_slice_idle = 0;

/* Default maximum budget values, in sectors and number of requests. */
static const int bfq_default_max_budget = 16 * 1024;
static const int bfq_max_budget_async_rq = 4;

/*
 * Async to sync throughput distribution is controlled as follows:
 * when an async request is served, the entity is charged the number
 * of sectors of the request, multipled by the factor below
 */
static const int bfq_async_charge_factor = 10;

/* Default timeout values, in jiffies, approximating CFQ defaults. */
static const int bfq_timeout_sync = HZ / 8;
static int bfq_timeout_async = HZ / 25;

struct kmem_cache *bfq_pool;
struct kmem_cache *bfq_ioc_pool;

static DEFINE_PER_CPU(unsigned long, bfq_ioc_count);
static struct completion *bfq_ioc_gone;
static DEFINE_SPINLOCK(bfq_ioc_gone_lock);

static DEFINE_SPINLOCK(cic_index_lock);
static DEFINE_IDA(cic_index_ida);

/* Below this threshold (in ms), we consider thinktime immediate. */
#define BFQ_MIN_TT		2

/* hw_tag detection: parallel requests threshold and min samples needed. */
#define BFQ_HW_QUEUE_THRESHOLD	4
#define BFQ_HW_QUEUE_SAMPLES	32

#define BFQQ_SEEK_THR	 (sector_t)(8 * 1024)
#define BFQQ_SEEKY(bfqq) ((bfqq)->seek_mean > BFQQ_SEEK_THR)

/* Min samples used for peak rate estimation (for autotuning). */
#define BFQ_PEAK_RATE_SAMPLES	32

/* Shift used for peak rate fixed precision calculations. */
#define BFQ_RATE_SHIFT		16

#define BFQ_SERVICE_TREE_INIT	((struct bfq_service_tree)		\
				{ RB_ROOT, RB_ROOT, NULL, NULL, 0, 0 })

#define RQ_CIC(rq)		\
	((struct cfq_io_context *) (rq)->elevator_private[0])
#define RQ_BFQQ(rq)		((rq)->elevator_private[1])

#include "bfq-ioc.c"
#include "bfq-sched.c"
#include "bfq-cgroup.c"

#define bfq_class_idle(bfqq)	((bfqq)->entity.ioprio_class ==\
				 IOPRIO_CLASS_IDLE)
#define bfq_class_rt(bfqq)	((bfqq)->entity.ioprio_class ==\
				 IOPRIO_CLASS_RT)

#define bfq_sample_valid(samples)	((samples) > 80)

/*
 * We regard a request as SYNC, if either it's a read or has the SYNC bit
 * set (in which case it could also be a direct WRITE).
 */
static inline int bfq_bio_sync(struct bio *bio)
{
	if (bio_data_dir(bio) == READ || (bio->bi_rw & REQ_SYNC))
		return 1;

	return 0;
}

/*
 * Scheduler run of queue, if there are requests pending and no one in the
 * driver that will restart queueing.
 */
static inline void bfq_schedule_dispatch(struct bfq_data *bfqd)
{
	if (bfqd->queued != 0) {
		bfq_log(bfqd, "schedule dispatch");
		kblockd_schedule_work(bfqd->queue, &bfqd->unplug_work);
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
		else {
			if (s1 >= s2)
				return rq1;
			else
				return rq2;
		}

	case BFQ_RQ2_WRAP:
		return rq1;
	case BFQ_RQ1_WRAP:
		return rq2;
	case (BFQ_RQ1_WRAP|BFQ_RQ2_WRAP): /* both rqs wrapped */
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
		(long long unsigned)sector,
		bfqq != NULL ? bfqq->pid : 0);

	return bfqq;
}

static void bfq_rq_pos_tree_add(struct bfq_data *bfqd, struct bfq_queue *bfqq)
{
	struct rb_node **p, *parent;
	struct bfq_queue *__bfqq;

	if (bfqq->pos_root != NULL) {
		rb_erase(&bfqq->pos_node, bfqq->pos_root);
		bfqq->pos_root = NULL;
	}

	if (bfq_class_idle(bfqq))
		return;
	if (!bfqq->next_rq)
		return;

	bfqq->pos_root = &bfqd->rq_pos_tree;
	__bfqq = bfq_rq_pos_tree_lookup(bfqd, bfqq->pos_root,
			blk_rq_pos(bfqq->next_rq), &parent, &p);
	if (__bfqq == NULL) {
		rb_link_node(&bfqq->pos_node, parent, p);
		rb_insert_color(&bfqq->pos_node, bfqq->pos_root);
	} else
		bfqq->pos_root = NULL;
}

static struct request *bfq_find_next_rq(struct bfq_data *bfqd,
					struct bfq_queue *bfqq,
					struct request *last)
{
	struct rb_node *rbnext = rb_next(&last->rb_node);
	struct rb_node *rbprev = rb_prev(&last->rb_node);
	struct request *next = NULL, *prev = NULL;

	BUG_ON(RB_EMPTY_NODE(&last->rb_node));

	if (rbprev != NULL)
		prev = rb_entry_rq(rbprev);

	if (rbnext != NULL)
		next = rb_entry_rq(rbnext);
	else {
		rbnext = rb_first(&bfqq->sort_list);
		if (rbnext && rbnext != &last->rb_node)
			next = rb_entry_rq(rbnext);
	}

	return bfq_choose_req(bfqd, next, prev, blk_rq_pos(last));
}

static void bfq_del_rq_rb(struct request *rq)
{
	struct bfq_queue *bfqq = RQ_BFQQ(rq);
	struct bfq_data *bfqd = bfqq->bfqd;
	const int sync = rq_is_sync(rq);

	BUG_ON(bfqq->queued[sync] == 0);
	bfqq->queued[sync]--;
	bfqd->queued--;

	elv_rb_del(&bfqq->sort_list, rq);

	if (RB_EMPTY_ROOT(&bfqq->sort_list)) {
		if (bfq_bfqq_busy(bfqq) && bfqq != bfqd->active_queue)
			bfq_del_bfqq_busy(bfqd, bfqq, 1);
		/*
		 * Remove queue from request-position tree as it is empty.
		 */
		if (bfqq->pos_root != NULL) {
			rb_erase(&bfqq->pos_node, bfqq->pos_root);
			bfqq->pos_root = NULL;
		}
	}
}

/* see the definition of bfq_async_charge_factor for details */
static inline unsigned long bfq_serv_to_charge(struct request *rq,
					       struct bfq_queue *bfqq)
{
	return blk_rq_sectors(rq) *
		(1 + ((!bfq_bfqq_sync(bfqq)) * (bfqq->raising_coeff == 1) *
		bfq_async_charge_factor));
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
	struct bfq_service_tree *st = bfq_entity_service_tree(entity);
	struct request *next_rq = bfqq->next_rq;
	unsigned long new_budget;

	if (next_rq == NULL)
		return;

	if (bfqq == bfqd->active_queue)
		/*
		 * In order not to break guarantees, budgets cannot be
		 * changed after an entity has been selected.
		 */
		return;

	BUG_ON(entity->tree != &st->active);
	BUG_ON(entity == entity->sched_data->active_entity);

	new_budget = max_t(unsigned long, bfqq->max_budget,
			   bfq_serv_to_charge(next_rq, bfqq));
	entity->budget = new_budget;
	bfq_log_bfqq(bfqd, bfqq, "updated next rq: new budget %lu", new_budget);
	bfq_activate_bfqq(bfqd, bfqq);
}

static void bfq_add_rq_rb(struct request *rq)
{
	struct bfq_queue *bfqq = RQ_BFQQ(rq);
	struct bfq_entity *entity = &bfqq->entity;
	struct bfq_data *bfqd = bfqq->bfqd;
	struct request *next_rq, *prev;
	unsigned long old_raising_coeff = bfqq->raising_coeff;
	int idle_for_long_time = bfqq->budget_timeout +
		bfqd->bfq_raising_min_idle_time < jiffies;

	bfq_log_bfqq(bfqd, bfqq, "add_rq_rb %d", rq_is_sync(rq));
	bfqq->queued[rq_is_sync(rq)]++;
	bfqd->queued++;

	elv_rb_add(&bfqq->sort_list, rq);

	/*
	 * Check if this request is a better next-serve candidate.
	 */
	prev = bfqq->next_rq;
	next_rq = bfq_choose_req(bfqd, bfqq->next_rq, rq, bfqd->last_position);
	BUG_ON(next_rq == NULL);
	bfqq->next_rq = next_rq;

	/*
	 * Adjust priority tree position, if next_rq changes.
	 */
	if (prev != bfqq->next_rq)
		bfq_rq_pos_tree_add(bfqd, bfqq);

	if (!bfq_bfqq_busy(bfqq)) {
		int soft_rt = bfqd->bfq_raising_max_softrt_rate > 0 &&
			bfqq->soft_rt_next_start < jiffies;
		entity->budget = max_t(unsigned long, bfqq->max_budget,
				       bfq_serv_to_charge(next_rq, bfqq));

		if (! bfqd->low_latency)
			goto add_bfqq_busy;

		/*
		 * If the queue is not being boosted and has been idle
		 * for enough time, start a weight-raising period
		 */
		if(old_raising_coeff == 1 && (idle_for_long_time || soft_rt)) {
			bfqq->raising_coeff = bfqd->bfq_raising_coeff;
			bfqq->raising_cur_max_time = idle_for_long_time ?
				bfqd->bfq_raising_max_time :
				bfqd->bfq_raising_rt_max_time;
			bfq_log_bfqq(bfqd, bfqq,
				     "wrais starting at %llu msec,"
				     "rais_max_time %u",
				     bfqq->last_rais_start_finish,
				     jiffies_to_msecs(bfqq->
					raising_cur_max_time));
		} else if (old_raising_coeff > 1) {
			if (idle_for_long_time)
				bfqq->raising_cur_max_time =
					bfqd->bfq_raising_max_time;
			else if (bfqq->raising_cur_max_time ==
				 bfqd->bfq_raising_rt_max_time &&
				 !soft_rt) {
				bfqq->raising_coeff = 1;
				bfq_log_bfqq(bfqd, bfqq,
					     "wrais ending at %llu msec,"
					     "rais_max_time %u",
					     bfqq->last_rais_start_finish,
					     jiffies_to_msecs(bfqq->
						raising_cur_max_time));
				}
		}
		if (old_raising_coeff != bfqq->raising_coeff)
			entity->ioprio_changed = 1;
add_bfqq_busy:
		bfq_add_bfqq_busy(bfqd, bfqq);
        } else {
                if(bfqd->low_latency && old_raising_coeff == 1 &&
			!rq_is_sync(rq) &&
			bfqq->last_rais_start_finish +
                        bfqd->bfq_raising_min_inter_arr_async < jiffies) {
                        bfqq->raising_coeff = bfqd->bfq_raising_coeff;
			bfqq->raising_cur_max_time = bfqd->bfq_raising_max_time;

			entity->ioprio_changed = 1;
			bfq_log_bfqq(bfqd, bfqq,
				     "non-idle wrais starting at %llu msec,"
				     "rais_max_time %u",
				     bfqq->last_rais_start_finish,
				     jiffies_to_msecs(bfqq->
					raising_cur_max_time));
                }
                bfq_updated_next_req(bfqd, bfqq);
	}

	if(bfqd->low_latency &&
		(old_raising_coeff == 1 || bfqq->raising_coeff == 1 ||
		 idle_for_long_time))
		bfqq->last_rais_start_finish = jiffies;
}

static void bfq_reposition_rq_rb(struct bfq_queue *bfqq, struct request *rq)
{
	elv_rb_del(&bfqq->sort_list, rq);
	bfqq->queued[rq_is_sync(rq)]--;
	bfqq->bfqd->queued--;
	bfq_add_rq_rb(rq);
}

static struct request *bfq_find_rq_fmerge(struct bfq_data *bfqd,
					  struct bio *bio)
{
	struct task_struct *tsk = current;
	struct cfq_io_context *cic;
	struct bfq_queue *bfqq;

	cic = bfq_cic_lookup(bfqd, tsk->io_context);
	if (cic == NULL)
		return NULL;

	bfqq = cic_to_bfqq(cic, bfq_bio_sync(bio));
	if (bfqq != NULL) {
		sector_t sector = bio->bi_sector + bio_sectors(bio);

		return elv_rb_find(&bfqq->sort_list, sector);
	}

	return NULL;
}

static void bfq_activate_request(struct request_queue *q, struct request *rq)
{
	struct bfq_data *bfqd = q->elevator->elevator_data;

	bfqd->rq_in_driver++;
	bfqd->last_position = blk_rq_pos(rq) + blk_rq_sectors(rq);
	bfq_log(bfqd, "activate_request: new bfqd->last_position %llu",
		(long long unsigned)bfqd->last_position);
}

static void bfq_deactivate_request(struct request_queue *q, struct request *rq)
{
	struct bfq_data *bfqd = q->elevator->elevator_data;

	WARN_ON(bfqd->rq_in_driver == 0);
	bfqd->rq_in_driver--;
}

static void bfq_remove_request(struct request *rq)
{
	struct bfq_queue *bfqq = RQ_BFQQ(rq);
	struct bfq_data *bfqd = bfqq->bfqd;

	if (bfqq->next_rq == rq) {
		bfqq->next_rq = bfq_find_next_rq(bfqd, bfqq, rq);
		bfq_updated_next_req(bfqd, bfqq);
	}

	list_del_init(&rq->queuelist);
	bfq_del_rq_rb(rq);

	if (rq->cmd_flags & REQ_META) {
		WARN_ON(bfqq->meta_pending == 0);
		bfqq->meta_pending--;
	}
}

static int bfq_merge(struct request_queue *q, struct request **req,
		     struct bio *bio)
{
	struct bfq_data *bfqd = q->elevator->elevator_data;
	struct request *__rq;

	__rq = bfq_find_rq_fmerge(bfqd, bio);
	if (__rq != NULL && elv_rq_merge_ok(__rq, bio)) {
		*req = __rq;
		return ELEVATOR_FRONT_MERGE;
	}

	return ELEVATOR_NO_MERGE;
}

static void bfq_merged_request(struct request_queue *q, struct request *req,
			       int type)
{
	if (type == ELEVATOR_FRONT_MERGE) {
		struct bfq_queue *bfqq = RQ_BFQQ(req);

		bfq_reposition_rq_rb(bfqq, req);
	}
}

static void bfq_merged_requests(struct request_queue *q, struct request *rq,
				struct request *next)
{
	struct bfq_queue *bfqq = RQ_BFQQ(rq);

	/*
	 * Reposition in fifo if next is older than rq.
	 */
	if (!list_empty(&rq->queuelist) && !list_empty(&next->queuelist) &&
	    time_before(rq_fifo_time(next), rq_fifo_time(rq))) {
		list_move(&rq->queuelist, &next->queuelist);
		rq_set_fifo_time(rq, rq_fifo_time(next));
	}

	if (bfqq->next_rq == next)
		bfqq->next_rq = rq;

	bfq_remove_request(next);
}

static int bfq_allow_merge(struct request_queue *q, struct request *rq,
			   struct bio *bio)
{
	struct bfq_data *bfqd = q->elevator->elevator_data;
	struct cfq_io_context *cic;
	struct bfq_queue *bfqq;

	/* Disallow merge of a sync bio into an async request. */
	if (bfq_bio_sync(bio) && !rq_is_sync(rq))
		return 0;

	/*
	 * Lookup the bfqq that this bio will be queued with. Allow
	 * merge only if rq is queued there.
	 */
	cic = bfq_cic_lookup(bfqd, current->io_context);
	if (cic == NULL)
		return 0;

	bfqq = cic_to_bfqq(cic, bfq_bio_sync(bio));
	return bfqq == RQ_BFQQ(rq);
}

static void __bfq_set_active_queue(struct bfq_data *bfqd,
				   struct bfq_queue *bfqq)
{
	if (bfqq != NULL) {
		bfq_mark_bfqq_must_alloc(bfqq);
		bfq_mark_bfqq_budget_new(bfqq);
		bfq_clear_bfqq_fifo_expire(bfqq);

		bfqd->budgets_assigned = (bfqd->budgets_assigned*7 + 256) / 8;

		bfq_log_bfqq(bfqd, bfqq, "set_active_queue, cur-budget = %lu",
			     bfqq->entity.budget);
	}

	bfqd->active_queue = bfqq;
}

/*
 * Get and set a new active queue for service.
 */
static struct bfq_queue *bfq_set_active_queue(struct bfq_data *bfqd,
					      struct bfq_queue *bfqq)
{
	if (!bfqq)
		bfqq = bfq_get_next_queue(bfqd);
	else
		bfq_get_next_queue_forced(bfqd, bfqq);

	__bfq_set_active_queue(bfqd, bfqq);
	return bfqq;
}

static inline sector_t bfq_dist_from_last(struct bfq_data *bfqd,
					  struct request *rq)
{
	if (blk_rq_pos(rq) >= bfqd->last_position)
		return blk_rq_pos(rq) - bfqd->last_position;
	else
		return bfqd->last_position - blk_rq_pos(rq);
}

/*
 * Return true if bfqq has no request pending and rq is close enough to
 * bfqd->last_position, or if rq is closer to bfqd->last_position than
 * bfqq->next_rq
 */
static inline int bfq_rq_close(struct bfq_data *bfqd, struct request *rq)
{
	return bfq_dist_from_last(bfqd, rq) <= BFQQ_SEEK_THR;
}

static struct bfq_queue *bfqq_close(struct bfq_data *bfqd)
{
	struct rb_root *root = &bfqd->rq_pos_tree;
	struct rb_node *parent, *node;
	struct bfq_queue *__bfqq;
	sector_t sector = bfqd->last_position;

	if (RB_EMPTY_ROOT(root))
		return NULL;

	/*
	 * First, if we find a request starting at the end of the last
	 * request, choose it.
	 */
	__bfqq = bfq_rq_pos_tree_lookup(bfqd, root, sector, &parent, NULL);
	if (__bfqq != NULL)
		return __bfqq;

	/*
	 * If the exact sector wasn't found, the parent of the NULL leaf
	 * will contain the closest sector (rq_pos_tree sorted by next_request
	 * position).
	 */
	__bfqq = rb_entry(parent, struct bfq_queue, pos_node);
	if (bfq_rq_close(bfqd, __bfqq->next_rq))
		return __bfqq;

	if (blk_rq_pos(__bfqq->next_rq) < sector)
		node = rb_next(&__bfqq->pos_node);
	else
		node = rb_prev(&__bfqq->pos_node);
	if (node == NULL)
		return NULL;

	__bfqq = rb_entry(node, struct bfq_queue, pos_node);
	if (bfq_rq_close(bfqd, __bfqq->next_rq))
		return __bfqq;

	return NULL;
}

/*
 * bfqd - obvious
 * cur_bfqq - passed in so that we don't decide that the current queue
 *            is closely cooperating with itself.
 *
 * We are assuming that cur_bfqq has dispatched at least one request,
 * and that bfqd->last_position reflects a position on the disk associated
 * with the I/O issued by cur_bfqq.
 */
static struct bfq_queue *bfq_close_cooperator(struct bfq_data *bfqd,
					      struct bfq_queue *cur_bfqq)
{
	struct bfq_queue *bfqq;

	if (bfq_class_idle(cur_bfqq))
		return NULL;
	if (!bfq_bfqq_sync(cur_bfqq))
		return NULL;
	if (BFQQ_SEEKY(cur_bfqq))
		return NULL;

	/* If device has only one backlogged bfq_queue, don't search. */
	if (bfqd->busy_queues == 1)
		return NULL;

	/*
	 * We should notice if some of the queues are cooperating, e.g.
	 * working closely on the same area of the disk. In that case,
	 * we can group them together and don't waste time idling.
	 */
	bfqq = bfqq_close(bfqd);
	if (bfqq == NULL || bfqq == cur_bfqq)
		return NULL;

	/*
	 * Do not merge queues from different bfq_groups.
	*/
	if (bfqq->entity.parent != cur_bfqq->entity.parent)
		return NULL;

	/*
	 * It only makes sense to merge sync queues.
	 */
	if (!bfq_bfqq_sync(bfqq))
		return NULL;
	if (BFQQ_SEEKY(bfqq))
		return NULL;

	/*
	 * Do not merge queues of different priority classes.
	 */
	if (bfq_class_rt(bfqq) != bfq_class_rt(cur_bfqq))
		return NULL;

	return bfqq;
}

/*
 * If enough samples have been computed, return the current max budget
 * stored in bfqd, which is dynamically updated according to the
 * estimated disk peak rate; otherwise return the default max budget
 */
static inline unsigned long bfq_max_budget(struct bfq_data *bfqd)
{
	return bfqd->budgets_assigned < 194 ? bfq_default_max_budget :
		bfqd->bfq_max_budget;
}

/*
 * Return min budget, which is a fraction of the current or default
 * max budget (trying with 1/32)
 */
static inline unsigned long bfq_min_budget(struct bfq_data *bfqd)
{
	return bfqd->budgets_assigned < 194 ? bfq_default_max_budget / 32 :
		bfqd->bfq_max_budget / 32;
}

/*
 * Decides whether idling should be done for given device and
 * given active queue.
 */
static inline bool bfq_queue_nonrot_noidle(struct bfq_data *bfqd,
					   struct bfq_queue *active_bfqq)
{
	if (active_bfqq == NULL)
		return false;
	/*
	 * If device is SSD it has no seek penalty, disable idling; but
	 * do so only if:
	 * - device does not support queuing, otherwise we still have
	 *   a problem with sync vs async workloads;
	 * - the queue is not weight-raised, to preserve guarantees.
	 */
	return (blk_queue_nonrot(bfqd->queue) && bfqd->hw_tag &&
		active_bfqq->raising_coeff == 1);
}

static void bfq_arm_slice_timer(struct bfq_data *bfqd)
{
	struct bfq_queue *bfqq = bfqd->active_queue;
	struct cfq_io_context *cic;
	unsigned long sl;

	WARN_ON(!RB_EMPTY_ROOT(&bfqq->sort_list));

	if (bfq_queue_nonrot_noidle(bfqd, bfqq))
		return;

	/* Idling is disabled, either manually or by past process history. */
	if (bfqd->bfq_slice_idle == 0 || !bfq_bfqq_idle_window(bfqq))
		return;

	/* Tasks have exited, don't wait. */
	cic = bfqd->active_cic;
	if (cic == NULL || atomic_read(&cic->ioc->nr_tasks) == 0)
		return;

	bfq_mark_bfqq_wait_request(bfqq);

	/*
	 * We don't want to idle for seeks, but we do want to allow
	 * fair distribution of slice time for a process doing back-to-back
	 * seeks. So allow a little bit of time for him to submit a new rq.
	 *
	 * To prevent processes with (partly) seeky workloads from
	 * being too ill-treated, grant them a small fraction of the
	 * assigned budget before reducing the waiting time to
	 * BFQ_MIN_TT. This happened to help reduce latency.
	 */
	sl = bfqd->bfq_slice_idle;
	if (bfq_sample_valid(bfqq->seek_samples) && BFQQ_SEEKY(bfqq) &&
	    bfqq->entity.service > bfq_max_budget(bfqd) / 8 &&
	    bfqq->raising_coeff == 1)
		sl = min(sl, msecs_to_jiffies(BFQ_MIN_TT));
	else if (bfqq->raising_coeff > 1)
		sl = sl * 3;
	bfqd->last_idling_start = ktime_get();
	mod_timer(&bfqd->idle_slice_timer, jiffies + sl);
	bfq_log(bfqd, "arm idle: %u/%u ms",
		jiffies_to_msecs(sl), jiffies_to_msecs(bfqd->bfq_slice_idle));
}

/*
 * Set the maximum time for the active queue to consume its
 * budget. This prevents seeky processes from lowering the disk
 * throughput (always guaranteed with a time slice scheme as in CFQ).
 */
static void bfq_set_budget_timeout(struct bfq_data *bfqd)
{
	struct bfq_queue *bfqq = bfqd->active_queue;
	unsigned int timeout_coeff =
		bfqq->raising_cur_max_time == bfqd->bfq_raising_rt_max_time ?
		1 : (bfqq->entity.weight / bfqq->entity.orig_weight);

	bfqd->last_budget_start = ktime_get();

	bfq_clear_bfqq_budget_new(bfqq);
	bfqq->budget_timeout = jiffies +
		bfqd->bfq_timeout[bfq_bfqq_sync(bfqq)] * timeout_coeff;

	bfq_log_bfqq(bfqd, bfqq, "set budget_timeout %u",
		jiffies_to_msecs(bfqd->bfq_timeout[bfq_bfqq_sync(bfqq)] *
		timeout_coeff));
}

/*
 * Move request from internal lists to the request queue dispatch list.
 */
static void bfq_dispatch_insert(struct request_queue *q, struct request *rq)
{
	struct bfq_data *bfqd = q->elevator->elevator_data;
	struct bfq_queue *bfqq = RQ_BFQQ(rq);

	bfq_remove_request(rq);
	bfqq->dispatched++;
	elv_dispatch_sort(q, rq);

	if (bfq_bfqq_sync(bfqq))
		bfqd->sync_flight++;
}

/*
 * Return expired entry, or NULL to just start from scratch in rbtree.
 */
static struct request *bfq_check_fifo(struct bfq_queue *bfqq)
{
	struct request *rq = NULL;

	if (bfq_bfqq_fifo_expire(bfqq))
		return NULL;

	bfq_mark_bfqq_fifo_expire(bfqq);

	if (list_empty(&bfqq->fifo))
		return NULL;

	rq = rq_entry_fifo(bfqq->fifo.next);

	if (time_before(jiffies, rq_fifo_time(rq)))
		return NULL;

	return rq;
}

/*
 * Must be called with the queue_lock held.
 */
static int bfqq_process_refs(struct bfq_queue *bfqq)
{
	int process_refs, io_refs;

	io_refs = bfqq->allocated[READ] + bfqq->allocated[WRITE];
	process_refs = atomic_read(&bfqq->ref) - io_refs - bfqq->entity.on_st;
	BUG_ON(process_refs < 0);
	return process_refs;
}

static void bfq_setup_merge(struct bfq_queue *bfqq, struct bfq_queue *new_bfqq)
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
		return;

	/* Avoid a circular list and skip interim queue merges. */
	while ((__bfqq = new_bfqq->new_bfqq)) {
		if (__bfqq == bfqq)
			return;
		new_bfqq = __bfqq;
	}

	process_refs = bfqq_process_refs(bfqq);
	new_process_refs = bfqq_process_refs(new_bfqq);
	/*
	 * If the process for the bfqq has gone away, there is no
	 * sense in merging the queues.
	 */
	if (process_refs == 0 || new_process_refs == 0)
		return;

	/*
	 * Merge in the direction of the lesser amount of work.
	 */
	if (new_process_refs >= process_refs) {
		bfqq->new_bfqq = new_bfqq;
		atomic_add(process_refs, &new_bfqq->ref);
	} else {
		new_bfqq->new_bfqq = bfqq;
		atomic_add(new_process_refs, &bfqq->ref);
	}
	bfq_log_bfqq(bfqq->bfqd, bfqq, "scheduling merge with queue %d",
		new_bfqq->pid);
}

static inline unsigned long bfq_bfqq_budget_left(struct bfq_queue *bfqq)
{
	struct bfq_entity *entity = &bfqq->entity;
	return entity->budget - entity->service;
}

static void __bfq_bfqq_expire(struct bfq_data *bfqd, struct bfq_queue *bfqq)
{
	BUG_ON(bfqq != bfqd->active_queue);

	__bfq_bfqd_reset_active(bfqd);

	if (RB_EMPTY_ROOT(&bfqq->sort_list)) {
		bfq_del_bfqq_busy(bfqd, bfqq, 1);
		/*
		 * overloading budget_timeout field to store when
		 * the queue remains with no backlog, used by
		 * the weight-raising mechanism
		 */
		bfqq->budget_timeout = jiffies ;
	}
	else {
		bfq_activate_bfqq(bfqd, bfqq);
		/*
		 * Resort priority tree of potential close cooperators.
		 */
		bfq_rq_pos_tree_add(bfqd, bfqq);
	}

	/*
	 * If this bfqq is shared between multiple processes, check
	 * to make sure that those processes are still issuing I/Os
	 * within the mean seek distance. If not, it may be time to
	 * break the queues apart again.
	 */
	if (bfq_bfqq_coop(bfqq) && BFQQ_SEEKY(bfqq))
		bfq_mark_bfqq_split_coop(bfqq);
}

/**
 * __bfq_bfqq_recalc_budget - try to adapt the budget to the @bfqq behavior.
 * @bfqd: device data.
 * @bfqq: queue to update.
 * @reason: reason for expiration.
 *
 * Handle the feedback on @bfqq budget.  See the body for detailed
 * comments.
 */
static void __bfq_bfqq_recalc_budget(struct bfq_data *bfqd,
				     struct bfq_queue *bfqq,
				     enum bfqq_expiration reason)
{
	struct request *next_rq;
	unsigned long budget, min_budget;

	budget = bfqq->max_budget;
	min_budget = bfq_min_budget(bfqd);

	BUG_ON(bfqq != bfqd->active_queue);

	bfq_log_bfqq(bfqd, bfqq, "recalc_budg: last budg %lu, budg left %lu",
		bfqq->entity.budget, bfq_bfqq_budget_left(bfqq));
	bfq_log_bfqq(bfqd, bfqq, "recalc_budg: last max_budg %lu, min budg %lu",
		budget, bfq_min_budget(bfqd));
	bfq_log_bfqq(bfqd, bfqq, "recalc_budg: sync %d, seeky %d",
		bfq_bfqq_sync(bfqq), BFQQ_SEEKY(bfqd->active_queue));

	if (bfq_bfqq_sync(bfqq)) {
		switch (reason) {
		/*
		 * Caveat: in all the following cases we trade latency
		 * for throughput.
		 */
		case BFQ_BFQQ_TOO_IDLE:
			/*
			 * This is the only case where we may reduce
			 * the budget: if there is no requets of the
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
			 * the still oustanding ones.  So in this
			 * subcase we do not reduce its budget, on the
			 * contrary we increase it to possibly boost
			 * the throughput, as discussed in the
			 * comments to the BUDGET_TIMEOUT case.
			 */
			if (bfqq->dispatched > 0) /* still oustanding reqs */
				budget = min(budget * 2, bfqd->bfq_max_budget);
			else {
				if (budget > 5 * min_budget)
					budget -= 4 * min_budget;
				else
					budget = min_budget;
			}
			break;
		case BFQ_BFQQ_BUDGET_TIMEOUT:
			/*
			 * We double the budget here because: 1) it
			 * gives the chance to boost the throughput if
			 * this is not a seeky process (which may have
			 * bumped into this timeout because of, e.g.,
			 * ZBR), 2) together with charge_full_budget
			 * it helps give seeky processes higher
			 * timestamps, and hence be served less
			 * frequently.
			 */
			budget = min(budget * 2, bfqd->bfq_max_budget);
			break;
		case BFQ_BFQQ_BUDGET_EXHAUSTED:
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
		case BFQ_BFQQ_NO_MORE_REQUESTS:
		       /*
			* Leave the budget unchanged.
			*/
		default:
			return;
		}
	} else /* async queue */
	    /* async queues get always the maximum possible budget
	     * (their ability to dispatch is limited by
	     * @bfqd->bfq_max_budget_async_rq).
	     */
		budget = bfqd->bfq_max_budget;

	bfqq->max_budget = budget;

	if (bfqd->budgets_assigned >= 194 && bfqd->bfq_user_max_budget == 0 &&
	    bfqq->max_budget > bfqd->bfq_max_budget)
		bfqq->max_budget = bfqd->bfq_max_budget;

	/*
	 * Make sure that we have enough budget for the next request.
	 * Since the finish time of the bfqq must be kept in sync with
	 * the budget, be sure to call __bfq_bfqq_expire() after the
	 * update.
	 */
	next_rq = bfqq->next_rq;
	if (next_rq != NULL)
		bfqq->entity.budget = max_t(unsigned long, bfqq->max_budget,
					    bfq_serv_to_charge(next_rq, bfqq));
	else
		bfqq->entity.budget = bfqq->max_budget;

	bfq_log_bfqq(bfqd, bfqq, "head sect: %u, new budget %lu",
			next_rq != NULL ? blk_rq_sectors(next_rq) : 0,
			bfqq->entity.budget);
}

static unsigned long bfq_calc_max_budget(u64 peak_rate, u64 timeout)
{
	unsigned long max_budget;

	/*
	 * The max_budget calculated when autotuning is equal to the
	 * amount of sectors transfered in timeout_sync at the
	 * estimated peak rate.
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
static int bfq_update_peak_rate(struct bfq_data *bfqd, struct bfq_queue *bfqq,
				int compensate, enum bfqq_expiration reason)
{
	u64 bw, usecs, expected, timeout;
	ktime_t delta;
	int update = 0;

	if (!bfq_bfqq_sync(bfqq) || bfq_bfqq_budget_new(bfqq))
		return 0;

	delta = compensate ? bfqd->last_idling_start : ktime_get();
	delta = ktime_sub(delta, bfqd->last_budget_start);
	usecs = ktime_to_us(delta);

	/* Don't trust short/unrealistic values. */
	if (usecs < 100 || usecs >= LONG_MAX)
		return 0;

	/*
	 * Calculate the bandwidth for the last slice.  We use a 64 bit
	 * value to store the peak rate, in sectors per usec in fixed
	 * point math.  We do so to have enough precision in the estimate
	 * and to avoid overflows.
	 */
	bw = (u64)bfqq->entity.service << BFQ_RATE_SHIFT;
	do_div(bw, (unsigned long)usecs);

	timeout = jiffies_to_msecs(bfqd->bfq_timeout[BLK_RW_SYNC]);

	/*
	 * Use only long (> 20ms) intervals to filter out spikes for
	 * the peak rate estimation.
	 */
	if (usecs > 20000) {
		if (bw > bfqd->peak_rate ||
		   (!BFQQ_SEEKY(bfqq) &&
		    reason == BFQ_BFQQ_BUDGET_TIMEOUT)) {
			bfq_log(bfqd, "measured bw =%llu", bw);
			/*
			 * To smooth oscillations use a low-pass filter with
			 * alpha=7/8, i.e.,
			 * new_rate = (7/8) * old_rate + (1/8) * bw
			 */
			do_div(bw, 8);
			bfqd->peak_rate *= 7;
			do_div(bfqd->peak_rate, 8);
			bfqd->peak_rate += bw;
			update = 1;
			bfq_log(bfqd, "new peak_rate=%llu", bfqd->peak_rate);
		}

		update |= bfqd->peak_rate_samples == BFQ_PEAK_RATE_SAMPLES - 1;

		if (bfqd->peak_rate_samples < BFQ_PEAK_RATE_SAMPLES)
			bfqd->peak_rate_samples++;

		if (bfqd->peak_rate_samples == BFQ_PEAK_RATE_SAMPLES &&
		    update && bfqd->bfq_user_max_budget == 0) {
			bfqd->bfq_max_budget =
				bfq_calc_max_budget(bfqd->peak_rate, timeout);
			bfq_log(bfqd, "new max_budget=%lu",
				bfqd->bfq_max_budget);
		}
	}

	/*
	 * If the process has been served for a too short time
	 * interval to let its possible sequential accesses prevail on
	 * the initial seek time needed to move the disk head on the
	 * first sector it requested, then give the process a chance
	 * and for the moment return false.
	 */
	if (bfqq->entity.budget <= bfq_max_budget(bfqd) / 8)
		return 0;

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

/**
 * bfq_bfqq_expire - expire a queue.
 * @bfqd: device owning the queue.
 * @bfqq: the queue to expire.
 * @compensate: if true, compensate for the time spent idling.
 * @reason: the reason causing the expiration.
 *
 *
 * If the process associated to the queue is slow (i.e., seeky), or in
 * case of budget timeout, or, finally, if it is async, we
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
			    int compensate,
			    enum bfqq_expiration reason)
{
	int slow;
	BUG_ON(bfqq != bfqd->active_queue);

	/* Update disk peak rate for autotuning and check whether the
	 * process is slow (see bfq_update_peak_rate).
	 */
	slow = bfq_update_peak_rate(bfqd, bfqq, compensate, reason);

	/*
	 * As above explained, 'punish' slow (i.e., seeky), timed-out
	 * and async queues, to favor sequential sync workloads.
	 *
	 * Processes doing IO in the slower disk zones will tend to be
	 * slow(er) even if not seeky. Hence, since the estimated peak
	 * rate is actually an average over the disk surface, these
	 * processes may timeout just for bad luck. To avoid punishing
	 * them we do not charge a full budget to a process that
	 * succeeded in consuming at least 2/3 of its budget.
	 */
	if (slow || (reason == BFQ_BFQQ_BUDGET_TIMEOUT &&
		     bfq_bfqq_budget_left(bfqq) >=  bfqq->entity.budget / 3))
		bfq_bfqq_charge_full_budget(bfqq);

	if (bfqd->low_latency && bfqq->raising_coeff == 1)
		bfqq->last_rais_start_finish = jiffies;

	if (bfqd->low_latency && bfqd->bfq_raising_max_softrt_rate > 0) {
	    if(reason != BFQ_BFQQ_BUDGET_TIMEOUT)
		bfqq->soft_rt_next_start =
			jiffies +
			HZ * bfqq->entity.service /
			bfqd->bfq_raising_max_softrt_rate;
		else
			bfqq->soft_rt_next_start = -1; /* infinity */
	}
	bfq_log_bfqq(bfqd, bfqq,
		"expire (%d, slow %d, num_disp %d, idle_win %d)", reason, slow,
		bfqq->dispatched, bfq_bfqq_idle_window(bfqq));

	/* Increase, decrease or leave budget unchanged according to reason */
	__bfq_bfqq_recalc_budget(bfqd, bfqq, reason);
	__bfq_bfqq_expire(bfqd, bfqq);
}

/*
 * Budget timeout is not implemented through a dedicated timer, but
 * just checked on request arrivals and completions, as well as on
 * idle timer expirations.
 */
static int bfq_bfqq_budget_timeout(struct bfq_queue *bfqq)
{
	if (bfq_bfqq_budget_new(bfqq))
		return 0;

	if (time_before(jiffies, bfqq->budget_timeout))
		return 0;

	return 1;
}

/*
 * If we expire a queue that is waiting for the arrival of a new
 * request, we may prevent the fictitious timestamp backshifting that
 * allows the guarantees of the queue to be preserved (see [1] for
 * this tricky aspect). Hence we return true only if this condition
 * does not hold, or if the queue is slow enough to deserve only to be
 * kicked off for preserving a high throughput.
*/
static inline int bfq_may_expire_for_budg_timeout(struct bfq_queue *bfqq)
{
	bfq_log_bfqq(bfqq->bfqd, bfqq,
		"may_budget_timeout: wr %d left %d timeout %d",
		bfq_bfqq_wait_request(bfqq),
			bfq_bfqq_budget_left(bfqq) >=  bfqq->entity.budget / 3,
		bfq_bfqq_budget_timeout(bfqq));

	return (!bfq_bfqq_wait_request(bfqq) ||
		bfq_bfqq_budget_left(bfqq) >=  bfqq->entity.budget / 3)
		&&
		bfq_bfqq_budget_timeout(bfqq);
}

/*
 * Select a queue for service.  If we have a current active queue,
 * check whether to continue servicing it, or retrieve and set a new one.
 */
static struct bfq_queue *bfq_select_queue(struct bfq_data *bfqd)
{
	struct bfq_queue *bfqq, *new_bfqq = NULL;
	struct request *next_rq;
	enum bfqq_expiration reason = BFQ_BFQQ_BUDGET_TIMEOUT;

	bfqq = bfqd->active_queue;
	if (bfqq == NULL)
		goto new_queue;

	bfq_log_bfqq(bfqd, bfqq, "select_queue: already active queue");

	/*
         * If another queue has a request waiting within our mean seek
         * distance, let it run. The expire code will check for close
         * cooperators and put the close queue at the front of the
         * service tree. If possible, merge the expiring queue with the
         * new bfqq.
         */
        new_bfqq = bfq_close_cooperator(bfqd, bfqq);
        if (new_bfqq != NULL && bfqq->new_bfqq == NULL)
                bfq_setup_merge(bfqq, new_bfqq);

	if (bfq_may_expire_for_budg_timeout(bfqq))
		goto expire;

	next_rq = bfqq->next_rq;
	/*
	 * If bfqq has requests queued and it has enough budget left to
	 * serve them, keep the queue, otherwise expire it.
	 */
	if (next_rq != NULL) {
		if (bfq_serv_to_charge(next_rq, bfqq) >
			bfq_bfqq_budget_left(bfqq)) {
			reason = BFQ_BFQQ_BUDGET_EXHAUSTED;
			goto expire;
		} else {
			/*
			 * The idle timer may be pending because we may not
			 * disable disk idling even when a new request arrives
			 */
			if (timer_pending(&bfqd->idle_slice_timer)) {
				/*
				 * If we get here: 1) at least a new request
				 * has arrived but we have not disabled the
				 * timer because the request was too small,
				 * 2) then the block layer has unplugged the
				 * device, causing the dispatch to be invoked.
				 *
				 * Since the device is unplugged, now the
				 * requests are probably large enough to
				 * provide a reasonable throughput.
				 * So we disable idling.
				 */
				bfq_clear_bfqq_wait_request(bfqq);
				del_timer(&bfqd->idle_slice_timer);
			}
			if (new_bfqq == NULL)
				goto keep_queue;
			else
				goto expire;
		}
	}

	/*
	 * No requests pending.  If there is no cooperator, and the active
	 * queue still has requests in flight or is idling for a new request,
	 * then keep it.
	 */
	if (new_bfqq == NULL && (timer_pending(&bfqd->idle_slice_timer) ||
		(bfqq->dispatched != 0 && bfq_bfqq_idle_window(bfqq) &&
		 !bfq_queue_nonrot_noidle(bfqd, bfqq)))) {
		bfqq = NULL;
		goto keep_queue;
	} else if (new_bfqq != NULL && timer_pending(&bfqd->idle_slice_timer)) {
		/*
		 * Expiring the queue because there is a close cooperator,
		 * cancel timer.
		 */
		bfq_clear_bfqq_wait_request(bfqq);
		del_timer(&bfqd->idle_slice_timer);
	}

	reason = BFQ_BFQQ_NO_MORE_REQUESTS;
expire:
	bfq_bfqq_expire(bfqd, bfqq, 0, reason);
new_queue:
	bfqq = bfq_set_active_queue(bfqd, new_bfqq);
	bfq_log(bfqd, "select_queue: new queue %d returned",
		bfqq != NULL ? bfqq->pid : 0);
keep_queue:
	return bfqq;
}

static void update_raising_data(struct bfq_data *bfqd, struct bfq_queue *bfqq)
{
	if (bfqq->raising_coeff > 1) { /* queue is being boosted */
		struct bfq_entity *entity = &bfqq->entity;

		bfq_log_bfqq(bfqd, bfqq,
			"raising period dur %u/%u msec, "
			"old raising coeff %u, w %d(%d)",
			jiffies_to_msecs(jiffies -
				bfqq->last_rais_start_finish),
			jiffies_to_msecs(bfqq->raising_cur_max_time),
			bfqq->raising_coeff,
			bfqq->entity.weight, bfqq->entity.orig_weight);

		BUG_ON(bfqq != bfqd->active_queue && entity->weight !=
			entity->orig_weight * bfqq->raising_coeff);
		if(entity->ioprio_changed)
			bfq_log_bfqq(bfqd, bfqq,
			"WARN: pending prio change");
		/*
		 * If too much time has elapsed from the beginning
		 * of this weight-raising period and process is not soft
		 * real-time, stop it
		 */
		if (jiffies - bfqq->last_rais_start_finish >
			bfqq->raising_cur_max_time) {
			int soft_rt = bfqd->bfq_raising_max_softrt_rate > 0 &&
				bfqq->soft_rt_next_start < jiffies;

			bfqq->last_rais_start_finish = jiffies;
			if (soft_rt)
				bfqq->raising_cur_max_time =
					bfqd->bfq_raising_rt_max_time;
			else {
				bfqq->raising_coeff = 1;
				entity->ioprio_changed = 1;
				__bfq_entity_update_weight_prio(
					bfq_entity_service_tree(entity),
					entity);
			}
		}
	}
}


/*
 * Dispatch one request from bfqq, moving it to the request queue
 * dispatch list.
 */
static int bfq_dispatch_request(struct bfq_data *bfqd,
				struct bfq_queue *bfqq)
{
	int dispatched = 0;
	struct request *rq;
	unsigned long service_to_charge;

	BUG_ON(RB_EMPTY_ROOT(&bfqq->sort_list));

	/* Follow expired path, else get first next available. */
	rq = bfq_check_fifo(bfqq);
	if (rq == NULL)
		rq = bfqq->next_rq;
	service_to_charge = bfq_serv_to_charge(rq, bfqq);

	if (service_to_charge > bfq_bfqq_budget_left(bfqq)) {
		/*
		 * This may happen if the next rq is chosen
		 * in fifo order instead of sector order.
		 * The budget is properly dimensioned
		 * to be always sufficient to serve the next request
		 * only if it is chosen in sector order. The reason is
		 * that it would be quite inefficient and little useful
		 * to always make sure that the budget is large enough
		 * to serve even the possible next rq in fifo order.
		 * In fact, requests are seldom served in fifo order.
		 *
		 * Expire the queue for budget exhaustion, and
		 * make sure that the next act_budget is enough
		 * to serve the next request, even if it comes
		 * from the fifo expired path.
		 */
		bfqq->next_rq = rq;
		/*
		 * Since this dispatch is failed, make sure that
		 * a new one will be performed
		 */
		if (!bfqd->rq_in_driver)
			bfq_schedule_dispatch(bfqd);
		goto expire;
	}

	/* Finally, insert request into driver dispatch list. */
	bfq_bfqq_served(bfqq, service_to_charge);
	bfq_dispatch_insert(bfqd->queue, rq);

	update_raising_data(bfqd, bfqq);

	bfq_log_bfqq(bfqd, bfqq, "dispatched %u sec req (%llu), "
			"budg left %lu",
			blk_rq_sectors(rq),
			(long long unsigned)blk_rq_pos(rq),
			bfq_bfqq_budget_left(bfqq));

	dispatched++;

	if (bfqd->active_cic == NULL) {
		atomic_long_inc(&RQ_CIC(rq)->ioc->refcount);
		bfqd->active_cic = RQ_CIC(rq);
	}

	if (bfqd->busy_queues > 1 && ((!bfq_bfqq_sync(bfqq) &&
	    dispatched >= bfqd->bfq_max_budget_async_rq) ||
	    bfq_class_idle(bfqq)))
		goto expire;

	return dispatched;

expire:
	bfq_bfqq_expire(bfqd, bfqq, 0, BFQ_BFQQ_BUDGET_EXHAUSTED);
	return dispatched;
}

static int __bfq_forced_dispatch_bfqq(struct bfq_queue *bfqq)
{
	int dispatched = 0;

	while (bfqq->next_rq != NULL) {
		bfq_dispatch_insert(bfqq->bfqd->queue, bfqq->next_rq);
		dispatched++;
	}

	BUG_ON(!list_empty(&bfqq->fifo));
	return dispatched;
}

/*
 * Drain our current requests.  Used for barriers and when switching
 * io schedulers on-the-fly.
 */
static int bfq_forced_dispatch(struct bfq_data *bfqd)
{
	struct bfq_queue *bfqq, *n;
	struct bfq_service_tree *st;
	int dispatched = 0;

	bfqq = bfqd->active_queue;
	if (bfqq != NULL)
		__bfq_bfqq_expire(bfqd, bfqq);

	/*
	 * Loop through classes, and be careful to leave the scheduler
	 * in a consistent state, as feedback mechanisms and vtime
	 * updates cannot be disabled during the process.
	 */
	list_for_each_entry_safe(bfqq, n, &bfqd->active_list, bfqq_list) {
		st = bfq_entity_service_tree(&bfqq->entity);

		dispatched += __bfq_forced_dispatch_bfqq(bfqq);
		bfqq->max_budget = bfq_max_budget(bfqd);

		bfq_forget_idle(st);
	}

	BUG_ON(bfqd->busy_queues != 0);

	return dispatched;
}

static int bfq_dispatch_requests(struct request_queue *q, int force)
{
	struct bfq_data *bfqd = q->elevator->elevator_data;
	struct bfq_queue *bfqq;
	int max_dispatch;

	bfq_log(bfqd, "dispatch requests: %d busy queues", bfqd->busy_queues);
	if (bfqd->busy_queues == 0)
		return 0;

	if (unlikely(force))
		return bfq_forced_dispatch(bfqd);

	if((bfqq = bfq_select_queue(bfqd)) == NULL)
		return 0;

	max_dispatch = bfqd->bfq_quantum;
	if (bfq_class_idle(bfqq))
		max_dispatch = 1;

	if (!bfq_bfqq_sync(bfqq))
		max_dispatch = bfqd->bfq_max_budget_async_rq;

	if (bfqq->dispatched >= max_dispatch) {
		if (bfqd->busy_queues > 1)
			return 0;
		if (bfqq->dispatched >= 4 * max_dispatch)
			return 0;
	}

	if (bfqd->sync_flight != 0 && !bfq_bfqq_sync(bfqq))
		return 0;

	bfq_clear_bfqq_wait_request(bfqq);
	BUG_ON(timer_pending(&bfqd->idle_slice_timer));

	if (! bfq_dispatch_request(bfqd, bfqq))
		return 0;

	bfq_log_bfqq(bfqd, bfqq, "dispatched one request of %d"
		     "(max_disp %d)", bfqq->pid, max_dispatch);

	return 1;
}

/*
 * Task holds one reference to the queue, dropped when task exits.  Each rq
 * in-flight on this queue also holds a reference, dropped when rq is freed.
 *
 * Queue lock must be held here.
 */
static void bfq_put_queue(struct bfq_queue *bfqq)
{
	struct bfq_data *bfqd = bfqq->bfqd;

	BUG_ON(atomic_read(&bfqq->ref) <= 0);

	bfq_log_bfqq(bfqd, bfqq, "put_queue: %p %d", bfqq,
		     atomic_read(&bfqq->ref));
	if (!atomic_dec_and_test(&bfqq->ref))
		return;

	BUG_ON(rb_first(&bfqq->sort_list) != NULL);
	BUG_ON(bfqq->allocated[READ] + bfqq->allocated[WRITE] != 0);
	BUG_ON(bfqq->entity.tree != NULL);
	BUG_ON(bfq_bfqq_busy(bfqq));
	BUG_ON(bfqd->active_queue == bfqq);

	bfq_log_bfqq(bfqd, bfqq, "put_queue: %p freed", bfqq);

	kmem_cache_free(bfq_pool, bfqq);
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
		if (__bfqq == bfqq) {
			WARN(1, "bfqq->new_bfqq loop detected.\n");
			break;
		}
		next = __bfqq->new_bfqq;
		bfq_put_queue(__bfqq);
		__bfqq = next;
	}
}

static void bfq_exit_bfqq(struct bfq_data *bfqd, struct bfq_queue *bfqq)
{
	if (bfqq == bfqd->active_queue) {
		__bfq_bfqq_expire(bfqd, bfqq);
		bfq_schedule_dispatch(bfqd);
	}

	bfq_log_bfqq(bfqd, bfqq, "exit_bfqq: %p, %d", bfqq,
		     atomic_read(&bfqq->ref));

	bfq_put_cooperator(bfqq);

	bfq_put_queue(bfqq);
}

/*
 * Update the entity prio values; note that the new values will not
 * be used until the next (re)activation.
 */
static void bfq_init_prio_data(struct bfq_queue *bfqq, struct io_context *ioc)
{
	struct task_struct *tsk = current;
	int ioprio_class;

	if (!bfq_bfqq_prio_changed(bfqq))
		return;

	ioprio_class = IOPRIO_PRIO_CLASS(ioc->ioprio);
	switch (ioprio_class) {
	default:
		printk(KERN_ERR "bfq: bad prio %x\n", ioprio_class);
	case IOPRIO_CLASS_NONE:
		/*
		 * No prio set, inherit CPU scheduling settings.
		 */
		bfqq->entity.new_ioprio = task_nice_ioprio(tsk);
		bfqq->entity.new_ioprio_class = task_nice_ioclass(tsk);
		break;
	case IOPRIO_CLASS_RT:
		bfqq->entity.new_ioprio = task_ioprio(ioc);
		bfqq->entity.new_ioprio_class = IOPRIO_CLASS_RT;
		break;
	case IOPRIO_CLASS_BE:
		bfqq->entity.new_ioprio = task_ioprio(ioc);
		bfqq->entity.new_ioprio_class = IOPRIO_CLASS_BE;
		break;
	case IOPRIO_CLASS_IDLE:
		bfqq->entity.new_ioprio_class = IOPRIO_CLASS_IDLE;
		bfqq->entity.new_ioprio = 7;
		bfq_clear_bfqq_idle_window(bfqq);
		break;
	}

	bfqq->entity.ioprio_changed = 1;

	/*
	 * Keep track of original prio settings in case we have to temporarily
	 * elevate the priority of this queue.
	 */
	bfqq->org_ioprio = bfqq->entity.new_ioprio;
	bfqq->org_ioprio_class = bfqq->entity.new_ioprio_class;
	bfq_clear_bfqq_prio_changed(bfqq);
}

static void bfq_changed_ioprio(struct io_context *ioc,
			       struct cfq_io_context *cic)
{
	struct bfq_data *bfqd;
	struct bfq_queue *bfqq, *new_bfqq;
	struct bfq_group *bfqg;
	unsigned long uninitialized_var(flags);

	bfqd = bfq_get_bfqd_locked(&cic->key, &flags);
	if (unlikely(bfqd == NULL))
		return;

	bfqq = cic->cfqq[BLK_RW_ASYNC];
	if (bfqq != NULL) {
		bfqg = container_of(bfqq->entity.sched_data, struct bfq_group,
				    sched_data);
		new_bfqq = bfq_get_queue(bfqd, bfqg, BLK_RW_ASYNC, cic->ioc,
					 GFP_ATOMIC);
		if (new_bfqq != NULL) {
			cic->cfqq[BLK_RW_ASYNC] = new_bfqq;
			bfq_log_bfqq(bfqd, bfqq,
				     "changed_ioprio: bfqq %p %d",
				     bfqq, atomic_read(&bfqq->ref));
			bfq_put_queue(bfqq);
		}
	}

	bfqq = cic->cfqq[BLK_RW_SYNC];
	if (bfqq != NULL)
		bfq_mark_bfqq_prio_changed(bfqq);

	bfq_put_bfqd_unlock(bfqd, &flags);
}

static void bfq_init_bfqq(struct bfq_data *bfqd, struct bfq_queue *bfqq,
			  pid_t pid, int is_sync)
{
	RB_CLEAR_NODE(&bfqq->entity.rb_node);
	INIT_LIST_HEAD(&bfqq->fifo);

	atomic_set(&bfqq->ref, 0);
	bfqq->bfqd = bfqd;

	bfq_mark_bfqq_prio_changed(bfqq);

	if (is_sync) {
		if (!bfq_class_idle(bfqq))
			bfq_mark_bfqq_idle_window(bfqq);
		bfq_mark_bfqq_sync(bfqq);
	}

	/* Tentative initial value to trade off between thr and lat */
	bfqq->max_budget = (2 * bfq_max_budget(bfqd)) / 3;
	bfqq->pid = pid;

	bfqq->raising_coeff = 1;
	bfqq->last_rais_start_finish = 0;
	bfqq->soft_rt_next_start = -1;
}

static struct bfq_queue *bfq_find_alloc_queue(struct bfq_data *bfqd,
					      struct bfq_group *bfqg,
					      int is_sync,
					      struct io_context *ioc,
					      gfp_t gfp_mask)
{
	struct bfq_queue *bfqq, *new_bfqq = NULL;
	struct cfq_io_context *cic;

retry:
	cic = bfq_cic_lookup(bfqd, ioc);
	/* cic always exists here */
	bfqq = cic_to_bfqq(cic, is_sync);

	/*
	 * Always try a new alloc if we fall back to the OOM bfqq
	 * originally, since it should just be a temporary situation.
	 */
	if (bfqq == NULL || bfqq == &bfqd->oom_bfqq) {
		bfqq = NULL;
		if (new_bfqq != NULL) {
			bfqq = new_bfqq;
			new_bfqq = NULL;
		} else if (gfp_mask & __GFP_WAIT) {
			spin_unlock_irq(bfqd->queue->queue_lock);
			new_bfqq = kmem_cache_alloc_node(bfq_pool,
					gfp_mask | __GFP_ZERO,
					bfqd->queue->node);
			spin_lock_irq(bfqd->queue->queue_lock);
			if (new_bfqq != NULL)
				goto retry;
		} else {
			bfqq = kmem_cache_alloc_node(bfq_pool,
					gfp_mask | __GFP_ZERO,
					bfqd->queue->node);
		}

		if (bfqq != NULL) {
			bfq_init_bfqq(bfqd, bfqq, current->pid, is_sync);
			bfq_log_bfqq(bfqd, bfqq, "allocated");
		} else {
			bfqq = &bfqd->oom_bfqq;
			bfq_log_bfqq(bfqd, bfqq, "using oom bfqq");
		}

		bfq_init_prio_data(bfqq, ioc);
		bfq_init_entity(&bfqq->entity, bfqg);
	}

	if (new_bfqq != NULL)
		kmem_cache_free(bfq_pool, new_bfqq);

	return bfqq;
}

static struct bfq_queue **bfq_async_queue_prio(struct bfq_data *bfqd,
					       struct bfq_group *bfqg,
					       int ioprio_class, int ioprio)
{
	switch (ioprio_class) {
	case IOPRIO_CLASS_RT:
		return &bfqg->async_bfqq[0][ioprio];
	case IOPRIO_CLASS_BE:
		return &bfqg->async_bfqq[1][ioprio];
	case IOPRIO_CLASS_IDLE:
		return &bfqg->async_idle_bfqq;
	default:
		BUG();
	}
}

static struct bfq_queue *bfq_get_queue(struct bfq_data *bfqd,
				       struct bfq_group *bfqg, int is_sync,
				       struct io_context *ioc, gfp_t gfp_mask)
{
	const int ioprio = task_ioprio(ioc);
	const int ioprio_class = task_ioprio_class(ioc);
	struct bfq_queue **async_bfqq = NULL;
	struct bfq_queue *bfqq = NULL;

	if (!is_sync) {
		async_bfqq = bfq_async_queue_prio(bfqd, bfqg, ioprio_class,
						  ioprio);
		bfqq = *async_bfqq;
	}

	if (bfqq == NULL)
		bfqq = bfq_find_alloc_queue(bfqd, bfqg, is_sync, ioc, gfp_mask);

	/*
	 * Pin the queue now that it's allocated, scheduler exit will prune it.
	 */
	if (!is_sync && *async_bfqq == NULL) {
		atomic_inc(&bfqq->ref);
		bfq_log_bfqq(bfqd, bfqq, "get_queue, bfqq not in async: %p, %d",
			     bfqq, atomic_read(&bfqq->ref));
		*async_bfqq = bfqq;
	}

	atomic_inc(&bfqq->ref);
	bfq_log_bfqq(bfqd, bfqq, "get_queue, at end: %p, %d", bfqq,
		     atomic_read(&bfqq->ref));
	return bfqq;
}

static void bfq_update_io_thinktime(struct bfq_data *bfqd,
				    struct cfq_io_context *cic)
{
	unsigned long elapsed = jiffies - cic->last_end_request;
	unsigned long ttime = min(elapsed, 2UL * bfqd->bfq_slice_idle);

	cic->ttime_samples = (7*cic->ttime_samples + 256) / 8;
	cic->ttime_total = (7*cic->ttime_total + 256*ttime) / 8;
	cic->ttime_mean = (cic->ttime_total + 128) / cic->ttime_samples;
}

static void bfq_update_io_seektime(struct bfq_data *bfqd,
				   struct bfq_queue *bfqq,
				   struct request *rq)
{
	sector_t sdist;
	u64 total;

	if (bfqq->last_request_pos < blk_rq_pos(rq))
		sdist = blk_rq_pos(rq) - bfqq->last_request_pos;
	else
		sdist = bfqq->last_request_pos - blk_rq_pos(rq);

	/*
	 * Don't allow the seek distance to get too large from the
	 * odd fragment, pagein, etc.
	 */
	if (bfqq->seek_samples == 0) /* first request, not really a seek */
		sdist = 0;
	else if (bfqq->seek_samples <= 60) /* second & third seek */
		sdist = min(sdist, (bfqq->seek_mean * 4) + 2*1024*1024);
	else
		sdist = min(sdist, (bfqq->seek_mean * 4) + 2*1024*64);

	bfqq->seek_samples = (7*bfqq->seek_samples + 256) / 8;
	bfqq->seek_total = (7*bfqq->seek_total + (u64)256*sdist) / 8;
	total = bfqq->seek_total + (bfqq->seek_samples/2);
	do_div(total, bfqq->seek_samples);
	if (bfq_bfqq_coop(bfqq)) {
		/*
		 * If the mean seektime increases for a (non-seeky) shared
		 * queue, some cooperator is likely to be idling too much.
		 * On the contrary,  if it decreases, some cooperator has
		 * probably waked up.
		 *
		 */
		if ((sector_t)total < bfqq->seek_mean)
			bfq_mark_bfqq_some_coop_idle(bfqq) ;
		else if ((sector_t)total > bfqq->seek_mean)
			bfq_clear_bfqq_some_coop_idle(bfqq) ;
	}
	bfqq->seek_mean = (sector_t)total;

	bfq_log_bfqq(bfqd, bfqq, "dist=%llu mean=%llu", (u64)sdist,
			(u64)bfqq->seek_mean);
}

/*
 * Disable idle window if the process thinks too long or seeks so much that
 * it doesn't matter.
 */
static void bfq_update_idle_window(struct bfq_data *bfqd,
				   struct bfq_queue *bfqq,
				   struct cfq_io_context *cic)
{
	int enable_idle;

	/* Don't idle for async or idle io prio class. */
	if (!bfq_bfqq_sync(bfqq) || bfq_class_idle(bfqq))
		return;

	enable_idle = bfq_bfqq_idle_window(bfqq);

	if (atomic_read(&cic->ioc->nr_tasks) == 0 ||
	    bfqd->bfq_slice_idle == 0 ||
		(bfqd->hw_tag && BFQQ_SEEKY(bfqq) &&
			bfqq->raising_coeff == 1))
		enable_idle = 0;
	else if (bfq_sample_valid(cic->ttime_samples)) {
		if (cic->ttime_mean > bfqd->bfq_slice_idle &&
			bfqq->raising_coeff == 1)
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
	struct cfq_io_context *cic = RQ_CIC(rq);

	if (rq->cmd_flags & REQ_META)
		bfqq->meta_pending++;

	bfq_update_io_thinktime(bfqd, cic);
	bfq_update_io_seektime(bfqd, bfqq, rq);
	if (bfqq->entity.service > bfq_max_budget(bfqd) / 8 ||
	    !BFQQ_SEEKY(bfqq))
		bfq_update_idle_window(bfqd, bfqq, cic);

	bfq_log_bfqq(bfqd, bfqq,
		     "rq_enqueued: idle_window=%d (seeky %d, mean %llu)",
		     bfq_bfqq_idle_window(bfqq), BFQQ_SEEKY(bfqq),
		     (long long unsigned)bfqq->seek_mean);

	bfqq->last_request_pos = blk_rq_pos(rq) + blk_rq_sectors(rq);

	if (bfqq == bfqd->active_queue) {
		/*
		 * If there is just this request queued and the request
		 * is small, just exit.
		 * In this way, if the disk is being idled to wait for a new
		 * request from the active queue, we avoid unplugging the
		 * device now.
		 *
		 * By doing so, we spare the disk to be committed
		 * to serve just a small request. On the contrary, we wait for
		 * the block layer to decide when to unplug the device:
		 * hopefully, new requests will be merged to this
		 * one quickly, then the device will be unplugged
		 * and larger requests will be dispatched.
		 */
	        if (bfqq->queued[rq_is_sync(rq)] == 1 &&
		    blk_rq_sectors(rq) < 32) {
		        return;
		}
		if (bfq_bfqq_wait_request(bfqq)) {
			/*
			 * If we are waiting for a request for this queue, let
			 * it rip immediately and flag that we must not expire
			 * this queue just now.
			 */
			bfq_clear_bfqq_wait_request(bfqq);
			del_timer(&bfqd->idle_slice_timer);
			/*
			 * Here we can safely expire the queue, in
			 * case of budget timeout, without wasting
			 * guarantees
			 */
			if (bfq_bfqq_budget_timeout(bfqq))
				bfq_bfqq_expire(bfqd, bfqq, 0,
						BFQ_BFQQ_BUDGET_TIMEOUT);
			__blk_run_queue(bfqd->queue);
		}
	}
}

static void bfq_insert_request(struct request_queue *q, struct request *rq)
{
	struct bfq_data *bfqd = q->elevator->elevator_data;
	struct bfq_queue *bfqq = RQ_BFQQ(rq);

	assert_spin_locked(bfqd->queue->queue_lock);
	bfq_init_prio_data(bfqq, RQ_CIC(rq)->ioc);

	bfq_add_rq_rb(rq);

	rq_set_fifo_time(rq, jiffies + bfqd->bfq_fifo_expire[rq_is_sync(rq)]);
	list_add_tail(&rq->queuelist, &bfqq->fifo);

	bfq_rq_enqueued(bfqd, bfqq, rq);
}

static void bfq_update_hw_tag(struct bfq_data *bfqd)
{
	bfqd->max_rq_in_driver = max(bfqd->max_rq_in_driver,
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

static void bfq_completed_request(struct request_queue *q, struct request *rq)
{
	struct bfq_queue *bfqq = RQ_BFQQ(rq);
	struct bfq_data *bfqd = bfqq->bfqd;
	const int sync = rq_is_sync(rq);

	bfq_log_bfqq(bfqd, bfqq, "completed %u sects req (%d)",
			blk_rq_sectors(rq), sync);

	bfq_update_hw_tag(bfqd);

	WARN_ON(!bfqd->rq_in_driver);
	WARN_ON(!bfqq->dispatched);
	bfqd->rq_in_driver--;
	bfqq->dispatched--;

	if (bfq_bfqq_sync(bfqq))
		bfqd->sync_flight--;

	if (sync)
		RQ_CIC(rq)->last_end_request = jiffies;

	/*
	 * If this is the active queue, check if it needs to be expired,
	 * or if we want to idle in case it has no pending requests.
	 */
	if (bfqd->active_queue == bfqq) {
		if (bfq_bfqq_budget_new(bfqq))
			bfq_set_budget_timeout(bfqd);

		/* Idling is disabled also for cooperation issues:
		 * 1) there is a close cooperator for the queue, or
		 * 2) the queue is shared and some cooperator is likely
		 *    to be idle (in this case, by not arming the idle timer,
		 *    we try to slow down the queue, to prevent the zones
		 *    of the disk accessed by the active cooperators to become
		 *    too distant from the zone that will be accessed by the
		 *    currently idle cooperators)
		 */
		if (bfq_may_expire_for_budg_timeout(bfqq))
			bfq_bfqq_expire(bfqd, bfqq, 0, BFQ_BFQQ_BUDGET_TIMEOUT);
		else if (sync &&
			(bfqd->rq_in_driver == 0 ||
				bfqq->raising_coeff > 1)
			&& RB_EMPTY_ROOT(&bfqq->sort_list)
			&& !bfq_close_cooperator(bfqd, bfqq)
			&& (!bfq_bfqq_coop(bfqq) ||
				!bfq_bfqq_some_coop_idle(bfqq)))
			bfq_arm_slice_timer(bfqd);
	}

	if (!bfqd->rq_in_driver)
		bfq_schedule_dispatch(bfqd);
}

/*
 * We temporarily boost lower priority queues if they are holding fs exclusive
 * resources.  They are boosted to normal prio (CLASS_BE/4).
 */
static void bfq_prio_boost(struct bfq_queue *bfqq)
{
	if (has_fs_excl()) {
		/*
		 * Boost idle prio on transactions that would lock out other
		 * users of the filesystem
		 */
		if (bfq_class_idle(bfqq))
			bfqq->entity.new_ioprio_class = IOPRIO_CLASS_BE;
		if (bfqq->entity.new_ioprio > IOPRIO_NORM)
			bfqq->entity.new_ioprio = IOPRIO_NORM;
	} else {
		/*
		 * Unboost the queue (if needed)
		 */
		bfqq->entity.new_ioprio_class = bfqq->org_ioprio_class;
		bfqq->entity.new_ioprio = bfqq->org_ioprio;
	}
}

static inline int __bfq_may_queue(struct bfq_queue *bfqq)
{
	if (bfq_bfqq_wait_request(bfqq) && bfq_bfqq_must_alloc(bfqq)) {
		bfq_clear_bfqq_must_alloc(bfqq);
		return ELV_MQUEUE_MUST;
	}

	return ELV_MQUEUE_MAY;
}

static int bfq_may_queue(struct request_queue *q, int rw)
{
	struct bfq_data *bfqd = q->elevator->elevator_data;
	struct task_struct *tsk = current;
	struct cfq_io_context *cic;
	struct bfq_queue *bfqq;

	/*
	 * Don't force setup of a queue from here, as a call to may_queue
	 * does not necessarily imply that a request actually will be queued.
	 * So just lookup a possibly existing queue, or return 'may queue'
	 * if that fails.
	 */
	cic = bfq_cic_lookup(bfqd, tsk->io_context);
	if (cic == NULL)
		return ELV_MQUEUE_MAY;

	bfqq = cic_to_bfqq(cic, rw_is_sync(rw));
	if (bfqq != NULL) {
		bfq_init_prio_data(bfqq, cic->ioc);
		bfq_prio_boost(bfqq);

		return __bfq_may_queue(bfqq);
	}

	return ELV_MQUEUE_MAY;
}

/*
 * Queue lock held here.
 */
static void bfq_put_request(struct request *rq)
{
	struct bfq_queue *bfqq = RQ_BFQQ(rq);

	if (bfqq != NULL) {
		const int rw = rq_data_dir(rq);

		BUG_ON(!bfqq->allocated[rw]);
		bfqq->allocated[rw]--;

		put_io_context(RQ_CIC(rq)->ioc);

		rq->elevator_private[0] = NULL;
		rq->elevator_private[1] = NULL;

		bfq_log_bfqq(bfqq->bfqd, bfqq, "put_request %p, %d",
			     bfqq, atomic_read(&bfqq->ref));
		bfq_put_queue(bfqq);
	}
}

static struct bfq_queue *
bfq_merge_bfqqs(struct bfq_data *bfqd, struct cfq_io_context *cic,
                struct bfq_queue *bfqq)
{
        bfq_log_bfqq(bfqd, bfqq, "merging with queue %lu",
		(long unsigned)bfqq->new_bfqq->pid);
        cic_set_bfqq(cic, bfqq->new_bfqq, 1);
        bfq_mark_bfqq_coop(bfqq->new_bfqq);
        bfq_put_queue(bfqq);
        return cic_to_bfqq(cic, 1);
}

/*
 * Returns NULL if a new bfqq should be allocated, or the old bfqq if this
 * was the last process referring to said bfqq.
 */
static struct bfq_queue *
bfq_split_bfqq(struct cfq_io_context *cic, struct bfq_queue *bfqq)
{
	bfq_log_bfqq(bfqq->bfqd, bfqq, "splitting queue");
	if (bfqq_process_refs(bfqq) == 1) {
		bfqq->pid = current->pid;
		bfq_clear_bfqq_some_coop_idle(bfqq);
		bfq_clear_bfqq_coop(bfqq);
		bfq_clear_bfqq_split_coop(bfqq);
		return bfqq;
	}

	cic_set_bfqq(cic, NULL, 1);

	bfq_put_cooperator(bfqq);

	bfq_put_queue(bfqq);
	return NULL;
}

/*
 * Allocate bfq data structures associated with this request.
 */
static int bfq_set_request(struct request_queue *q, struct request *rq,
			   gfp_t gfp_mask)
{
	struct bfq_data *bfqd = q->elevator->elevator_data;
	struct cfq_io_context *cic;
	const int rw = rq_data_dir(rq);
	const int is_sync = rq_is_sync(rq);
	struct bfq_queue *bfqq;
	struct bfq_group *bfqg;
	unsigned long flags;

	might_sleep_if(gfp_mask & __GFP_WAIT);

	cic = bfq_get_io_context(bfqd, gfp_mask);

	spin_lock_irqsave(q->queue_lock, flags);

	if (cic == NULL)
		goto queue_fail;

	bfqg = bfq_cic_update_cgroup(cic);

new_queue:
	bfqq = cic_to_bfqq(cic, is_sync);
	if (bfqq == NULL || bfqq == &bfqd->oom_bfqq) {
		bfqq = bfq_get_queue(bfqd, bfqg, is_sync, cic->ioc, gfp_mask);
		cic_set_bfqq(cic, bfqq, is_sync);
	} else {
		/*
		 * If the queue was seeky for too long, break it apart.
		 */
		if (bfq_bfqq_coop(bfqq) && bfq_bfqq_split_coop(bfqq)) {
			bfq_log_bfqq(bfqd, bfqq, "breaking apart bfqq");
			bfqq = bfq_split_bfqq(cic, bfqq);
			if (!bfqq)
				goto new_queue;
		}

		/*
		 * Check to see if this queue is scheduled to merge with
		 * another closely cooperating queue. The merging of queues
		 * happens here as it must be done in process context.
		 * The reference on new_bfqq was taken in merge_bfqqs.
		 */
		if (bfqq->new_bfqq != NULL)
			bfqq = bfq_merge_bfqqs(bfqd, cic, bfqq);
	}

	bfqq->allocated[rw]++;
	atomic_inc(&bfqq->ref);
	bfq_log_bfqq(bfqd, bfqq, "set_request: bfqq %p, %d", bfqq,
		     atomic_read(&bfqq->ref));

	spin_unlock_irqrestore(q->queue_lock, flags);

	rq->elevator_private[0] = cic;
	rq->elevator_private[1] = bfqq;

	return 0;

queue_fail:
	if (cic != NULL)
		put_io_context(cic->ioc);

	bfq_schedule_dispatch(bfqd);
	spin_unlock_irqrestore(q->queue_lock, flags);

	return 1;
}

static void bfq_kick_queue(struct work_struct *work)
{
	struct bfq_data *bfqd =
		container_of(work, struct bfq_data, unplug_work);
	struct request_queue *q = bfqd->queue;

	spin_lock_irq(q->queue_lock);
	__blk_run_queue(q);
	spin_unlock_irq(q->queue_lock);
}

/*
 * Handler of the expiration of the timer running if the active_queue
 * is idling inside its time slice.
 */
static void bfq_idle_slice_timer(unsigned long data)
{
	struct bfq_data *bfqd = (struct bfq_data *)data;
	struct bfq_queue *bfqq;
	unsigned long flags;
	enum bfqq_expiration reason;

	spin_lock_irqsave(bfqd->queue->queue_lock, flags);

	bfqq = bfqd->active_queue;
	/*
	 * Theoretical race here: active_queue can be NULL or different
	 * from the queue that was idling if the timer handler spins on
	 * the queue_lock and a new request arrives for the current
	 * queue and there is a full dispatch cycle that changes the
	 * active_queue.  This can hardly happen, but in the worst case
	 * we just expire a queue too early.
	 */
	if (bfqq != NULL) {
		bfq_log_bfqq(bfqd, bfqq, "slice_timer expired");
		if (bfq_bfqq_budget_timeout(bfqq))
			/*
			 * Also here the queue can be safely expired
			 * for budget timeout without wasting
			 * guarantees
			 */
			reason = BFQ_BFQQ_BUDGET_TIMEOUT;
		else if (bfqq->queued[0] == 0 && bfqq->queued[1] == 0)
			/*
			 * The queue may not be empty upon timer expiration,
			 * because we may not disable the timer when the first
			 * request of the active queue arrives during
			 * disk idling
			 */
			reason = BFQ_BFQQ_TOO_IDLE;
		else
			goto schedule_dispatch;

		bfq_bfqq_expire(bfqd, bfqq, 1, reason);
	}

schedule_dispatch:
	bfq_schedule_dispatch(bfqd);

	spin_unlock_irqrestore(bfqd->queue->queue_lock, flags);
}

static void bfq_shutdown_timer_wq(struct bfq_data *bfqd)
{
	del_timer_sync(&bfqd->idle_slice_timer);
	cancel_work_sync(&bfqd->unplug_work);
}

static inline void __bfq_put_async_bfqq(struct bfq_data *bfqd,
					struct bfq_queue **bfqq_ptr)
{
	struct bfq_group *root_group = bfqd->root_group;
	struct bfq_queue *bfqq = *bfqq_ptr;

	bfq_log(bfqd, "put_async_bfqq: %p", bfqq);
	if (bfqq != NULL) {
		bfq_bfqq_move(bfqd, bfqq, &bfqq->entity, root_group);
		bfq_log_bfqq(bfqd, bfqq, "put_async_bfqq: putting %p, %d",
			     bfqq, atomic_read(&bfqq->ref));
		bfq_put_queue(bfqq);
		*bfqq_ptr = NULL;
	}
}

/*
 * Release all the bfqg references to its async queues.  If we are
 * deallocating the group these queues may still contain requests, so
 * we reparent them to the root cgroup (i.e., the only one that will
 * exist for sure untill all the requests on a device are gone).
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
	struct request_queue *q = bfqd->queue;
	struct bfq_queue *bfqq, *n;
	struct cfq_io_context *cic;

	bfq_shutdown_timer_wq(bfqd);

	spin_lock_irq(q->queue_lock);

	while (!list_empty(&bfqd->cic_list)) {
		cic = list_entry(bfqd->cic_list.next, struct cfq_io_context,
				 queue_list);
		__bfq_exit_single_io_context(bfqd, cic);
	}

	BUG_ON(bfqd->active_queue != NULL);
	list_for_each_entry_safe(bfqq, n, &bfqd->idle_list, bfqq_list)
		bfq_deactivate_bfqq(bfqd, bfqq, 0);

	bfq_disconnect_groups(bfqd);
	spin_unlock_irq(q->queue_lock);

	bfq_shutdown_timer_wq(bfqd);

	spin_lock(&cic_index_lock);
	ida_remove(&cic_index_ida, bfqd->cic_index);
	spin_unlock(&cic_index_lock);

	/* Wait for cic->key accessors to exit their grace periods. */
	synchronize_rcu();

	BUG_ON(timer_pending(&bfqd->idle_slice_timer));

	bfq_free_root_group(bfqd);
	kfree(bfqd);
}

static int bfq_alloc_cic_index(void)
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

static void *bfq_init_queue(struct request_queue *q)
{
	struct bfq_group *bfqg;
	struct bfq_data *bfqd;
	int i;

	i = bfq_alloc_cic_index();
	if (i < 0)
		return NULL;

	bfqd = kmalloc_node(sizeof(*bfqd), GFP_KERNEL | __GFP_ZERO, q->node);
	if (bfqd == NULL)
		return NULL;

	bfqd->cic_index = i;

	/*
	 * Our fallback bfqq if bfq_find_alloc_queue() runs into OOM issues.
	 * Grab a permanent reference to it, so that the normal code flow
	 * will not attempt to free it.
	 */
	bfq_init_bfqq(bfqd, &bfqd->oom_bfqq, 1, 0);
	atomic_inc(&bfqd->oom_bfqq.ref);

	INIT_LIST_HEAD(&bfqd->cic_list);

	bfqd->queue = q;

	bfqg = bfq_alloc_root_group(bfqd, q->node);
	if (bfqg == NULL) {
		kfree(bfqd);
		return NULL;
	}

	bfqd->root_group = bfqg;

	init_timer(&bfqd->idle_slice_timer);
	bfqd->idle_slice_timer.function = bfq_idle_slice_timer;
	bfqd->idle_slice_timer.data = (unsigned long)bfqd;

	bfqd->rq_pos_tree = RB_ROOT;

	INIT_WORK(&bfqd->unplug_work, bfq_kick_queue);

	INIT_LIST_HEAD(&bfqd->active_list);
	INIT_LIST_HEAD(&bfqd->idle_list);

	bfqd->hw_tag = -1;

	bfqd->bfq_max_budget = bfq_default_max_budget;

	bfqd->bfq_quantum = bfq_quantum;
	bfqd->bfq_fifo_expire[0] = bfq_fifo_expire[0];
	bfqd->bfq_fifo_expire[1] = bfq_fifo_expire[1];
	bfqd->bfq_back_max = bfq_back_max;
	bfqd->bfq_back_penalty = bfq_back_penalty;
	bfqd->bfq_slice_idle = bfq_slice_idle;
	bfqd->bfq_class_idle_last_service = 0;
	bfqd->bfq_max_budget_async_rq = bfq_max_budget_async_rq;
	bfqd->bfq_timeout[BLK_RW_ASYNC] = bfq_timeout_async;
	bfqd->bfq_timeout[BLK_RW_SYNC] = bfq_timeout_sync;

	bfqd->low_latency = true;

	bfqd->bfq_raising_coeff = 20;
	bfqd->bfq_raising_rt_max_time = msecs_to_jiffies(300);
	bfqd->bfq_raising_max_time = msecs_to_jiffies(7500);
	bfqd->bfq_raising_min_idle_time = msecs_to_jiffies(2000);
	bfqd->bfq_raising_min_inter_arr_async = msecs_to_jiffies(500);
	bfqd->bfq_raising_max_softrt_rate = 7000;

	return bfqd;
}

static void bfq_slab_kill(void)
{
	if (bfq_pool != NULL)
		kmem_cache_destroy(bfq_pool);
	if (bfq_ioc_pool != NULL)
		kmem_cache_destroy(bfq_ioc_pool);
}

static int __init bfq_slab_setup(void)
{
	bfq_pool = KMEM_CACHE(bfq_queue, 0);
	if (bfq_pool == NULL)
		goto fail;

	bfq_ioc_pool = kmem_cache_create("bfq_io_context",
					 sizeof(struct cfq_io_context),
					 __alignof__(struct cfq_io_context),
					 0, NULL);
	if (bfq_ioc_pool == NULL)
		goto fail;

	return 0;
fail:
	bfq_slab_kill();
	return -ENOMEM;
}

static ssize_t bfq_var_show(unsigned int var, char *page)
{
	return sprintf(page, "%d\n", var);
}

static ssize_t bfq_var_store(unsigned long *var, const char *page, size_t count)
{
	unsigned long new_val;
	int ret = strict_strtoul(page, 10, &new_val);

	if (ret == 0)
		*var = new_val;

	return count;
}

static ssize_t bfq_weights_show(struct elevator_queue *e, char *page)
{
	struct bfq_queue *bfqq;
	struct bfq_data *bfqd = e->elevator_data;
	ssize_t num_char = 0;

	num_char += sprintf(page + num_char, "Active:\n");
	list_for_each_entry(bfqq, &bfqd->active_list, bfqq_list) {
		num_char += sprintf(page + num_char,
			"pid%d: weight %hu, dur %d/%u\n",
			bfqq->pid,
			bfqq->entity.weight,
			jiffies_to_msecs(jiffies -
				bfqq->last_rais_start_finish),
			jiffies_to_msecs(bfqq->raising_cur_max_time));
	}
	num_char += sprintf(page + num_char, "Idle:\n");
	list_for_each_entry(bfqq, &bfqd->idle_list, bfqq_list) {
			num_char += sprintf(page + num_char,
				"pid%d: weight %hu, dur %d/%u\n",
				bfqq->pid,
				bfqq->entity.weight,
				jiffies_to_msecs(jiffies -
					bfqq->last_rais_start_finish),
				jiffies_to_msecs(bfqq->raising_cur_max_time));
	}
	return num_char;
}

#define SHOW_FUNCTION(__FUNC, __VAR, __CONV)				\
static ssize_t __FUNC(struct elevator_queue *e, char *page)		\
{									\
	struct bfq_data *bfqd = e->elevator_data;			\
	unsigned int __data = __VAR;					\
	if (__CONV)							\
		__data = jiffies_to_msecs(__data);			\
	return bfq_var_show(__data, (page));				\
}
SHOW_FUNCTION(bfq_quantum_show, bfqd->bfq_quantum, 0);
SHOW_FUNCTION(bfq_fifo_expire_sync_show, bfqd->bfq_fifo_expire[1], 1);
SHOW_FUNCTION(bfq_fifo_expire_async_show, bfqd->bfq_fifo_expire[0], 1);
SHOW_FUNCTION(bfq_back_seek_max_show, bfqd->bfq_back_max, 0);
SHOW_FUNCTION(bfq_back_seek_penalty_show, bfqd->bfq_back_penalty, 0);
SHOW_FUNCTION(bfq_slice_idle_show, bfqd->bfq_slice_idle, 1);
SHOW_FUNCTION(bfq_max_budget_show, bfqd->bfq_user_max_budget, 0);
SHOW_FUNCTION(bfq_max_budget_async_rq_show, bfqd->bfq_max_budget_async_rq, 0);
SHOW_FUNCTION(bfq_timeout_sync_show, bfqd->bfq_timeout[BLK_RW_SYNC], 1);
SHOW_FUNCTION(bfq_timeout_async_show, bfqd->bfq_timeout[BLK_RW_ASYNC], 1);
SHOW_FUNCTION(bfq_low_latency_show, bfqd->low_latency, 0);
SHOW_FUNCTION(bfq_raising_coeff_show, bfqd->bfq_raising_coeff, 0);
SHOW_FUNCTION(bfq_raising_max_time_show, bfqd->bfq_raising_max_time, 1);
SHOW_FUNCTION(bfq_raising_rt_max_time_show, bfqd->bfq_raising_rt_max_time, 1);
SHOW_FUNCTION(bfq_raising_min_idle_time_show, bfqd->bfq_raising_min_idle_time,
	1);
SHOW_FUNCTION(bfq_raising_min_inter_arr_async_show,
	      bfqd->bfq_raising_min_inter_arr_async,
	      1);
SHOW_FUNCTION(bfq_raising_max_softrt_rate_show,
	bfqd->bfq_raising_max_softrt_rate, 0);
#undef SHOW_FUNCTION

#define STORE_FUNCTION(__FUNC, __PTR, MIN, MAX, __CONV)			\
static ssize_t								\
__FUNC(struct elevator_queue *e, const char *page, size_t count)	\
{									\
	struct bfq_data *bfqd = e->elevator_data;			\
	unsigned long __data;						\
	int ret = bfq_var_store(&__data, (page), count);		\
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
STORE_FUNCTION(bfq_quantum_store, &bfqd->bfq_quantum, 1, INT_MAX, 0);
STORE_FUNCTION(bfq_fifo_expire_sync_store, &bfqd->bfq_fifo_expire[1], 1,
		INT_MAX, 1);
STORE_FUNCTION(bfq_fifo_expire_async_store, &bfqd->bfq_fifo_expire[0], 1,
		INT_MAX, 1);
STORE_FUNCTION(bfq_back_seek_max_store, &bfqd->bfq_back_max, 0, INT_MAX, 0);
STORE_FUNCTION(bfq_back_seek_penalty_store, &bfqd->bfq_back_penalty, 1,
		INT_MAX, 0);
STORE_FUNCTION(bfq_slice_idle_store, &bfqd->bfq_slice_idle, 0, INT_MAX, 1);
STORE_FUNCTION(bfq_max_budget_async_rq_store, &bfqd->bfq_max_budget_async_rq,
		1, INT_MAX, 0);
STORE_FUNCTION(bfq_timeout_async_store, &bfqd->bfq_timeout[BLK_RW_ASYNC], 0,
		INT_MAX, 1);
STORE_FUNCTION(bfq_raising_coeff_store, &bfqd->bfq_raising_coeff, 1,
		INT_MAX, 0);
STORE_FUNCTION(bfq_raising_max_time_store, &bfqd->bfq_raising_max_time, 0,
		INT_MAX, 1);
STORE_FUNCTION(bfq_raising_rt_max_time_store, &bfqd->bfq_raising_rt_max_time, 0,
		INT_MAX, 1);
STORE_FUNCTION(bfq_raising_min_idle_time_store,
	       &bfqd->bfq_raising_min_idle_time, 0, INT_MAX, 1);
STORE_FUNCTION(bfq_raising_min_inter_arr_async_store,
	       &bfqd->bfq_raising_min_inter_arr_async, 0, INT_MAX, 1);
STORE_FUNCTION(bfq_raising_max_softrt_rate_store,
	       &bfqd->bfq_raising_max_softrt_rate, 0, INT_MAX, 0);
#undef STORE_FUNCTION

/* do nothing for the moment */
static ssize_t bfq_weights_store(struct elevator_queue *e,
				    const char *page, size_t count)
{
	return count;
}

static inline unsigned long bfq_estimated_max_budget(struct bfq_data *bfqd)
{
	u64 timeout = jiffies_to_msecs(bfqd->bfq_timeout[BLK_RW_SYNC]);

	if (bfqd->peak_rate_samples >= BFQ_PEAK_RATE_SAMPLES)
		return bfq_calc_max_budget(bfqd->peak_rate, timeout);
	else
		return bfq_default_max_budget;
}

static ssize_t bfq_max_budget_store(struct elevator_queue *e,
				    const char *page, size_t count)
{
	struct bfq_data *bfqd = e->elevator_data;
	unsigned long __data;
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

static ssize_t bfq_timeout_sync_store(struct elevator_queue *e,
				      const char *page, size_t count)
{
	struct bfq_data *bfqd = e->elevator_data;
	unsigned long __data;
	int ret = bfq_var_store(&__data, (page), count);

	if (__data < 1)
		__data = 1;
	else if (__data > INT_MAX)
		__data = INT_MAX;

	bfqd->bfq_timeout[BLK_RW_SYNC] = msecs_to_jiffies(__data);
	if (bfqd->bfq_user_max_budget == 0)
		bfqd->bfq_max_budget = bfq_estimated_max_budget(bfqd);

	return ret;
}

static ssize_t bfq_low_latency_store(struct elevator_queue *e,
				     const char *page, size_t count)
{
	struct bfq_data *bfqd = e->elevator_data;
	unsigned long __data;
	int ret = bfq_var_store(&__data, (page), count);

	if (__data > 1)
		__data = 1;
	bfqd->low_latency = __data;

	return ret;
}

#define BFQ_ATTR(name) \
	__ATTR(name, S_IRUGO|S_IWUSR, bfq_##name##_show, bfq_##name##_store)

static struct elv_fs_entry bfq_attrs[] = {
	BFQ_ATTR(quantum),
	BFQ_ATTR(fifo_expire_sync),
	BFQ_ATTR(fifo_expire_async),
	BFQ_ATTR(back_seek_max),
	BFQ_ATTR(back_seek_penalty),
	BFQ_ATTR(slice_idle),
	BFQ_ATTR(max_budget),
	BFQ_ATTR(max_budget_async_rq),
	BFQ_ATTR(timeout_sync),
	BFQ_ATTR(timeout_async),
	BFQ_ATTR(low_latency),
	BFQ_ATTR(raising_coeff),
	BFQ_ATTR(raising_max_time),
	BFQ_ATTR(raising_rt_max_time),
	BFQ_ATTR(raising_min_idle_time),
	BFQ_ATTR(raising_min_inter_arr_async),
	BFQ_ATTR(raising_max_softrt_rate),
	BFQ_ATTR(weights),
	__ATTR_NULL
};

static struct elevator_type iosched_bfq = {
	.ops = {
		.elevator_merge_fn =		bfq_merge,
		.elevator_merged_fn =		bfq_merged_request,
		.elevator_merge_req_fn =	bfq_merged_requests,
		.elevator_allow_merge_fn =	bfq_allow_merge,
		.elevator_dispatch_fn =		bfq_dispatch_requests,
		.elevator_add_req_fn =		bfq_insert_request,
		.elevator_activate_req_fn =	bfq_activate_request,
		.elevator_deactivate_req_fn =	bfq_deactivate_request,
		.elevator_completed_req_fn =	bfq_completed_request,
		.elevator_former_req_fn =	elv_rb_former_request,
		.elevator_latter_req_fn =	elv_rb_latter_request,
		.elevator_set_req_fn =		bfq_set_request,
		.elevator_put_req_fn =		bfq_put_request,
		.elevator_may_queue_fn =	bfq_may_queue,
		.elevator_init_fn =		bfq_init_queue,
		.elevator_exit_fn =		bfq_exit_queue,
		.trim =				bfq_free_io_context,
	},
	.elevator_attrs =	bfq_attrs,
	.elevator_name =	"bfq",
	.elevator_owner =	THIS_MODULE,
};

static int __init bfq_init(void)
{
	/*
	 * Can be 0 on HZ < 1000 setups.
	 */
	//if (bfq_slice_idle == 0)
	//	bfq_slice_idle = 1;

	if (bfq_timeout_async == 0)
		bfq_timeout_async = 1;

	if (bfq_slab_setup())
		return -ENOMEM;

	elv_register(&iosched_bfq);

	return 0;
}

static void __exit bfq_exit(void)
{
	DECLARE_COMPLETION_ONSTACK(all_gone);
	elv_unregister(&iosched_bfq);
	bfq_ioc_gone = &all_gone;
	/* bfq_ioc_gone's update must be visible before reading bfq_ioc_count */
	smp_wmb();
	if (elv_ioc_count_read(bfq_ioc_count) != 0)
		wait_for_completion(&all_gone);
	ida_destroy(&cic_index_ida);
	bfq_slab_kill();
}

module_init(bfq_init);
module_exit(bfq_exit);

MODULE_AUTHOR("Fabio Checconi, Paolo Valente");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Budget Fair Queueing IO scheduler");
