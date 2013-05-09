/*
 * Budget Fair Queueing (BFQ) disk scheduler.
 *
 * Based on ideas and code from CFQ:
 * Copyright (C) 2003 Jens Axboe <axboe@kernel.dk>
 *
 * Copyright (C) 2008 Fabio Checconi <fabio@gandalf.sssup.it>
 *		      Paolo Valente <paolo.valente@unimore.it>
 *
 * Copyright (C) 2010 Paolo Valente <paolo.valente@unimore.it>
 *
 * Licensed under the GPL-2 as detailed in the accompanying COPYING.BFQ
 * file.
 *
 * BFQ is a proportional-share storage-I/O scheduling algorithm based on
 * the slice-by-slice service scheme of CFQ. But BFQ assigns budgets,
 * measured in number of sectors, to processes instead of time slices. The
 * device is not granted to the in-service process for a given time slice,
 * but until it has exhausted its assigned budget. This change from the time
 * to the service domain allows BFQ to distribute the device throughput
 * among processes as desired, without any distortion due to ZBR, workload
 * fluctuations or other factors. BFQ uses an ad hoc internal scheduler,
 * called B-WF2Q+, to schedule processes according to their budgets. More
 * precisely, BFQ schedules queues associated to processes. Thanks to the
 * accurate policy of B-WF2Q+, BFQ can afford to assign high budgets to
 * I/O-bound processes issuing sequential requests (to boost the
 * throughput), and yet guarantee a low latency to interactive and soft
 * real-time applications.
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
 * B-WF2Q+ is based on WF2Q+, that is described in [2], together with
 * H-WF2Q+, while the augmented tree used to implement B-WF2Q+ with O(log N)
 * complexity derives from the one introduced with EEVDF in [3].
 *
 * [1] P. Valente and M. Andreolini, ``Improving Application Responsiveness
 *     with the BFQ Disk I/O Scheduler'',
 *     Proceedings of the 5th Annual International Systems and Storage
 *     Conference (SYSTOR '12), June 2012.
 *
 * http://algogroup.unimo.it/people/paolo/disk_sched/bf1-v1-suite-results.pdf
 *
 * [2] Jon C.R. Bennett and H. Zhang, ``Hierarchical Packet Fair Queueing
 *     Algorithms,'' IEEE/ACM Transactions on Networking, 5(5):675-689,
 *     Oct 1997.
 *
 * http://www.cs.cmu.edu/~hzhang/papers/TON-97-Oct.ps.gz
 *
 * [3] I. Stoica and H. Abdel-Wahab, ``Earliest Eligible Virtual Deadline
 *     First: A Flexible and Accurate Mechanism for Proportional Share
 *     Resource Allocation,'' technical report.
 *
 * http://www.cs.berkeley.edu/~istoica/papers/eevdf-tr-95.pdf
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
#include "blk.h"

/* Expiration time of sync (0) and async (1) requests, in jiffies. */
static const int bfq_fifo_expire[2] = { HZ / 4, HZ / 8 };

/* Maximum backwards seek, in KiB. */
static const int bfq_back_max = 16 * 1024;

/* Penalty of a backwards seek, in number of sectors. */
static const int bfq_back_penalty = 2;

/* Idling period duration, in jiffies. */
static int bfq_slice_idle = HZ / 125;

/* Minimum number of assigned budgets for which stats are safe to compute. */
static const int bfq_stats_min_budgets = 194;

/* Default maximum budget values, in sectors and number of requests. */
static const int bfq_default_max_budget = 16 * 1024;
static const int bfq_max_budget_async_rq = 4;

/*
 * Async to sync throughput distribution is controlled as follows:
 * when an async request is served, the entity is charged the number
 * of sectors of the request, multiplied by the factor below
 */
static const int bfq_async_charge_factor = 10;

/* Default timeout values, in jiffies, approximating CFQ defaults. */
static const int bfq_timeout_sync = HZ / 8;
static int bfq_timeout_async = HZ / 25;

struct kmem_cache *bfq_pool;

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
 * In the following definitions, R_slow[0]/R_fast[0] and T_slow[0]/T_fast[0]
 * are the reference values for a slow/fast rotational device, whereas
 * R_slow[1]/R_fast[1] and T_slow[1]/T_fast[1] are the reference values for
 * a slow/fast non-rotational device. Finally, device_speed_thresh are the
 * thresholds used to switch between speed classes.
 * Both the reference peak rates and the thresholds are measured in
 * sectors/usec, left-shifted by BFQ_RATE_SHIFT.
 */
static int R_slow[2] = {1536, 10752};
static int R_fast[2] = {17415, 34791};
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

static void bfq_schedule_dispatch(struct bfq_data *bfqd);

#include "bfq-ioc.c"
#include "bfq-sched.c"
#include "bfq-cgroup.c"

#define bfq_class_idle(bfqq)	((bfqq)->ioprio_class == IOPRIO_CLASS_IDLE)
#define bfq_class_rt(bfqq)	((bfqq)->ioprio_class == IOPRIO_CLASS_RT)

#define bfq_sample_valid(samples)	((samples) > 80)

/*
 * We regard a request as SYNC, if either it's a read or has the SYNC bit
 * set (in which case it could also be a direct WRITE).
 */
static int bfq_bio_sync(struct bio *bio)
{
	if (bio_data_dir(bio) == READ || (bio->bi_rw & REQ_SYNC))
		return 1;

	return 0;
}

/*
 * Scheduler run of queue, if there are requests pending and no one in the
 * driver that will restart queueing.
 */
static void bfq_schedule_dispatch(struct bfq_data *bfqd)
{
	if (bfqd->queued != 0) {
		bfq_log(bfqd, "schedule dispatch");
		kblockd_schedule_work(&bfqd->unplug_work);
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
	return
#ifdef CONFIG_BFQ_GROUP_IOSCHED
		!bfqd->active_numerous_groups &&
#endif
		!bfq_differentiated_weights(bfqd);
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

	BUG_ON(RB_EMPTY_ROOT(root));
	BUG_ON(entity->weight_counter->weight != entity->weight);

	BUG_ON(!entity->weight_counter->num_active);
	entity->weight_counter->num_active--;
	if (entity->weight_counter->num_active > 0)
		goto reset_entity_pointer;

	rb_erase(&entity->weight_counter->weights_node, root);
	kfree(entity->weight_counter);

reset_entity_pointer:
	entity->weight_counter = NULL;
}

static struct request *bfq_find_next_rq(struct bfq_data *bfqd,
					struct bfq_queue *bfqq,
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
	return blk_rq_sectors(rq) *
		(1 + ((!bfq_bfqq_sync(bfqq)) * (bfqq->wr_coeff == 1) *
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

	if (!next_rq)
		return;

	if (bfqq == bfqd->in_service_queue)
		/*
		 * In order not to break guarantees, budgets cannot be
		 * changed after an entity has been selected.
		 */
		return;

	BUG_ON(entity->tree != &st->active);
	BUG_ON(entity == entity->sched_data->in_service_entity);

	new_budget = max_t(unsigned long, bfqq->max_budget,
			   bfq_serv_to_charge(next_rq, bfqq));
	if (entity->budget != new_budget) {
		entity->budget = new_budget;
		bfq_log_bfqq(bfqd, bfqq, "updated next rq: new budget %lu",
					 new_budget);
		bfq_activate_bfqq(bfqd, bfqq);
	}
}

static unsigned int bfq_wr_duration(struct bfq_data *bfqd)
{
	u64 dur;

	if (bfqd->bfq_wr_max_time > 0)
		return bfqd->bfq_wr_max_time;

	dur = bfqd->RT_prod;
	do_div(dur, bfqd->peak_rate);

	return dur;
}

/* Empty burst list and add just bfqq (see comments to bfq_handle_burst) */
static void bfq_reset_burst_list(struct bfq_data *bfqd, struct bfq_queue *bfqq)
{
	struct bfq_queue *item;
	struct hlist_node *n;

	hlist_for_each_entry_safe(item, n, &bfqd->burst_list, burst_list_node)
		hlist_del_init(&item->burst_list_node);
	hlist_add_head(&bfqq->burst_list_node, &bfqd->burst_list);
	bfqd->burst_size = 1;
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
	} else /* burst not yet large: add bfqq to the burst list */
		hlist_add_head(&bfqq->burst_list_node, &bfqd->burst_list);
}

/*
 * If many queues happen to become active shortly after each other, then,
 * to help the processes associated to these queues get their job done as
 * soon as possible, it is usually better to not grant either weight-raising
 * or device idling to these queues. In this comment we describe, firstly,
 * the reasons why this fact holds, and, secondly, the next function, which
 * implements the main steps needed to properly mark these queues so that
 * they can then be treated in a different way.
 *
 * As for the terminology, we say that a queue becomes active, i.e.,
 * switches from idle to backlogged, either when it is created (as a
 * consequence of the arrival of an I/O request), or, if already existing,
 * when a new request for the queue arrives while the queue is idle.
 * Bursts of activations, i.e., activations of different queues occurring
 * shortly after each other, are typically caused by services or applications
 * that spawn or reactivate many parallel threads/processes. Examples are
 * systemd during boot or git grep.
 *
 * These services or applications benefit mostly from a high throughput:
 * the quicker the requests of the activated queues are cumulatively served,
 * the sooner the target job of these queues gets completed. As a consequence,
 * weight-raising any of these queues, which also implies idling the device
 * for it, is almost always counterproductive: in most cases it just lowers
 * throughput.
 *
 * On the other hand, a burst of activations may be also caused by the start
 * of an application that does not consist in a lot of parallel I/O-bound
 * threads. In fact, with a complex application, the burst may be just a
 * consequence of the fact that several processes need to be executed to
 * start-up the application. To start an application as quickly as possible,
 * the best thing to do is to privilege the I/O related to the application
 * with respect to all other I/O. Therefore, the best strategy to start as
 * quickly as possible an application that causes a burst of activations is
 * to weight-raise all the queues activated during the burst. This is the
 * exact opposite of the best strategy for the other type of bursts.
 *
 * In the end, to take the best action for each of the two cases, the two
 * types of bursts need to be distinguished. Fortunately, this seems
 * relatively easy to do, by looking at the sizes of the bursts. In
 * particular, we found a threshold such that bursts with a larger size
 * than that threshold are apparently caused only by services or commands
 * such as systemd or git grep. For brevity, hereafter we call just 'large'
 * these bursts. BFQ *does not* weight-raise queues whose activations occur
 * in a large burst. In addition, for each of these queues BFQ performs or
 * does not perform idling depending on which choice boosts the throughput
 * most. The exact choice depends on the device and request pattern at
 * hand.
 *
 * Turning back to the next function, it implements all the steps needed
 * to detect the occurrence of a large burst and to properly mark all the
 * queues belonging to it (so that they can then be treated in a different
 * way). This goal is achieved by maintaining a special "burst list" that
 * holds, temporarily, the queues that belong to the burst in progress. The
 * list is then used to mark these queues as belonging to a large burst if
 * the burst does become large. The main steps are the following.
 *
 * . when the very first queue is activated, the queue is inserted into the
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
 * . if a queue Q that does not belong to the burst is activated while
 *   the device is in large-burst mode and shortly after the last time
 *   at which a queue either entered the burst list or was marked as
 *   belonging to the current large burst, then Q is immediately marked
 *   as belonging to a large burst.
 *
 * . if a queue Q that does not belong to the burst is activated a while
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
static void bfq_handle_burst(struct bfq_data *bfqd, struct bfq_queue *bfqq,
			     bool idle_for_long_time)
{
	/*
	 * If bfqq happened to be activated in a burst, but has been idle
	 * for at least as long as an interactive queue, then we assume
	 * that, in the overall I/O initiated in the burst, the I/O
	 * associated to bfqq is finished. So bfqq does not need to be
	 * treated as a queue belonging to a burst anymore. Accordingly,
	 * we reset bfqq's in_large_burst flag if set, and remove bfqq
	 * from the burst list if it's there. We do not decrement instead
	 * burst_size, because the fact that bfqq does not need to belong
	 * to the burst list any more does not invalidate the fact that
	 * bfqq may have been activated during the current burst.
	 */
	if (idle_for_long_time) {
		hlist_del_init(&bfqq->burst_list_node);
		bfq_clear_bfqq_in_large_burst(bfqq);
	}

	/*
	 * If bfqq is already in the burst list or is part of a large
	 * burst, then there is nothing else to do.
	 */
	if (!hlist_unhashed(&bfqq->burst_list_node) ||
	    bfq_bfqq_in_large_burst(bfqq))
		return;

	/*
	 * If bfqq's activation happens late enough, then the current
	 * burst is finished, and related data structures must be reset.
	 *
	 * In this respect, consider the special case where bfqq is the very
	 * first queue being activated. In this case, last_ins_in_burst is
	 * not yet significant when we get here. But it is easy to verify
	 * that, whether or not the following condition is true, bfqq will
	 * end up being inserted into the burst list. In particular the
	 * list will happen to contain only bfqq. And this is exactly what
	 * has to happen, as bfqq may be the first queue in a possible
	 * burst.
	 */
	if (time_is_before_jiffies(bfqd->last_ins_in_burst +
	    bfqd->bfq_burst_interval)) {
		bfqd->large_burst = false;
		bfq_reset_burst_list(bfqd, bfqq);
		return;
	}

	/*
	 * If we get here, then bfqq is being activated shortly after the
	 * last queue. So, if the current burst is also large, we can mark
	 * bfqq as belonging to this large burst immediately.
	 */
	if (bfqd->large_burst) {
		bfq_mark_bfqq_in_large_burst(bfqq);
		return;
	}

	/*
	 * If we get here, then a large-burst state has not yet been
	 * reached, but bfqq is being activated shortly after the last
	 * queue. Then we add bfqq to the burst.
	 */
	bfq_add_to_burst(bfqd, bfqq);
}

static void bfq_add_request(struct request *rq)
{
	struct bfq_queue *bfqq = RQ_BFQQ(rq);
	struct bfq_entity *entity = &bfqq->entity;
	struct bfq_data *bfqd = bfqq->bfqd;
	struct request *next_rq, *prev;
	unsigned long old_wr_coeff = bfqq->wr_coeff;
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
	BUG_ON(!next_rq);
	bfqq->next_rq = next_rq;

	if (!bfq_bfqq_busy(bfqq)) {
		bool soft_rt, in_burst,
		     idle_for_long_time = time_is_before_jiffies(
						bfqq->budget_timeout +
						bfqd->bfq_wr_min_idle_time);

#ifdef CONFIG_BFQ_GROUP_IOSCHED
		bfqg_stats_update_io_add(bfqq_group(RQ_BFQQ(rq)), bfqq,
					 rq->cmd_flags);
#endif
		if (bfq_bfqq_sync(bfqq)) {
			bool already_in_burst =
			   !hlist_unhashed(&bfqq->burst_list_node) ||
			   bfq_bfqq_in_large_burst(bfqq);
			bfq_handle_burst(bfqd, bfqq, idle_for_long_time);
			/*
			 * If bfqq was not already in the current burst,
			 * then, at this point, bfqq either has been
			 * added to the current burst or has caused the
			 * current burst to terminate. In particular, in
			 * the second case, bfqq has become the first
			 * queue in a possible new burst.
			 * In both cases last_ins_in_burst needs to be
			 * moved forward.
			 */
			if (!already_in_burst)
				bfqd->last_ins_in_burst = jiffies;
		}

		in_burst = bfq_bfqq_in_large_burst(bfqq);
		soft_rt = bfqd->bfq_wr_max_softrt_rate > 0 &&
			!in_burst &&
			time_is_before_jiffies(bfqq->soft_rt_next_start);
		interactive = !in_burst && idle_for_long_time;
		entity->budget = max_t(unsigned long, bfqq->max_budget,
				       bfq_serv_to_charge(next_rq, bfqq));

		if (!bfq_bfqq_IO_bound(bfqq)) {
			if (time_before(jiffies,
					RQ_BIC(rq)->ttime.last_end_request +
					bfqd->bfq_slice_idle)) {
				bfqq->requests_within_timer++;
				if (bfqq->requests_within_timer >=
				    bfqd->bfq_requests_within_timer)
					bfq_mark_bfqq_IO_bound(bfqq);
			} else
				bfqq->requests_within_timer = 0;
		}

		if (!bfqd->low_latency)
			goto add_bfqq_busy;

		/*
		 * If the queue:
		 * - is not being boosted,
		 * - has been idle for enough time,
		 * - is not a sync queue or is linked to a bfq_io_cq (it is
		 *   shared "for its nature" or it is not shared and its
		 *   requests have not been redirected to a shared queue)
		 * start a weight-raising period.
		 */
		if (old_wr_coeff == 1 && (interactive || soft_rt) &&
		    (!bfq_bfqq_sync(bfqq) || bfqq->bic)) {
			bfqq->wr_coeff = bfqd->bfq_wr_coeff;
			if (interactive)
				bfqq->wr_cur_max_time = bfq_wr_duration(bfqd);
			else
				bfqq->wr_cur_max_time =
					bfqd->bfq_wr_rt_max_time;
			bfq_log_bfqq(bfqd, bfqq,
				     "wrais starting at %lu, rais_max_time %u",
				     jiffies,
				     jiffies_to_msecs(bfqq->wr_cur_max_time));
		} else if (old_wr_coeff > 1) {
			if (interactive)
				bfqq->wr_cur_max_time = bfq_wr_duration(bfqd);
			else if (in_burst ||
				 (bfqq->wr_cur_max_time ==
				  bfqd->bfq_wr_rt_max_time &&
				  !soft_rt)) {
				bfqq->wr_coeff = 1;
				bfq_log_bfqq(bfqd, bfqq,
					"wrais ending at %lu, rais_max_time %u",
					jiffies,
					jiffies_to_msecs(bfqq->
						wr_cur_max_time));
			} else if (time_before(
					bfqq->last_wr_start_finish +
					bfqq->wr_cur_max_time,
					jiffies +
					bfqd->bfq_wr_rt_max_time) &&
				   soft_rt) {
				/*
				 *
				 * The remaining weight-raising time is lower
				 * than bfqd->bfq_wr_rt_max_time, which means
				 * that the application is enjoying weight
				 * raising either because deemed soft-rt in
				 * the near past, or because deemed interactive
				 * a long ago.
				 * In both cases, resetting now the current
				 * remaining weight-raising time for the
				 * application to the weight-raising duration
				 * for soft rt applications would not cause any
				 * latency increase for the application (as the
				 * new duration would be higher than the
				 * remaining time).
				 *
				 * In addition, the application is now meeting
				 * the requirements for being deemed soft rt.
				 * In the end we can correctly and safely
				 * (re)charge the weight-raising duration for
				 * the application with the weight-raising
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
				bfqq->last_wr_start_finish = jiffies;
				bfqq->wr_cur_max_time =
					bfqd->bfq_wr_rt_max_time;
			}
		}
		if (old_wr_coeff != bfqq->wr_coeff)
			entity->prio_changed = 1;
add_bfqq_busy:
		bfqq->last_idle_bklogged = jiffies;
		bfqq->service_from_backlogged = 0;
		bfq_clear_bfqq_softrt_update(bfqq);
		bfq_add_bfqq_busy(bfqd, bfqq);
	} else {
		if (bfqd->low_latency && old_wr_coeff == 1 && !rq_is_sync(rq) &&
		    time_is_before_jiffies(
				bfqq->last_wr_start_finish +
				bfqd->bfq_wr_min_inter_arr_async)) {
			bfqq->wr_coeff = bfqd->bfq_wr_coeff;
			bfqq->wr_cur_max_time = bfq_wr_duration(bfqd);

			bfqd->wr_busy_queues++;
			entity->prio_changed = 1;
			bfq_log_bfqq(bfqd, bfqq,
			    "non-idle wrais starting at %lu, rais_max_time %u",
			    jiffies,
			    jiffies_to_msecs(bfqq->wr_cur_max_time));
		}
		if (prev != bfqq->next_rq)
			bfq_updated_next_req(bfqd, bfqq);
	}

	if (bfqd->low_latency &&
		(old_wr_coeff == 1 || bfqq->wr_coeff == 1 || interactive))
		bfqq->last_wr_start_finish = jiffies;
}

static struct request *bfq_find_rq_fmerge(struct bfq_data *bfqd,
					  struct bio *bio)
{
	struct task_struct *tsk = current;
	struct bfq_io_cq *bic;
	struct bfq_queue *bfqq;

	bic = bfq_bic_lookup(bfqd, tsk->io_context);
	if (!bic)
		return NULL;

	bfqq = bic_to_bfqq(bic, bfq_bio_sync(bio));
	if (bfqq)
		return elv_rb_find(&bfqq->sort_list, bio_end_sector(bio));

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

	BUG_ON(bfqd->rq_in_driver == 0);
	bfqd->rq_in_driver--;
}

static void bfq_remove_request(struct request *rq)
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
	BUG_ON(bfqq->queued[sync] == 0);
	bfqq->queued[sync]--;
	bfqd->queued--;
	elv_rb_del(&bfqq->sort_list, rq);

	if (RB_EMPTY_ROOT(&bfqq->sort_list)) {
		if (bfq_bfqq_busy(bfqq) && bfqq != bfqd->in_service_queue)
			bfq_del_bfqq_busy(bfqd, bfqq, 1);
		/*
		 * Remove queue from request-position tree as it is empty.
		 */
		if (bfqq->pos_root) {
			rb_erase(&bfqq->pos_node, bfqq->pos_root);
			bfqq->pos_root = NULL;
		}
	}

	if (rq->cmd_flags & REQ_META) {
		BUG_ON(bfqq->meta_pending == 0);
		bfqq->meta_pending--;
	}
#ifdef CONFIG_BFQ_GROUP_IOSCHED
	bfqg_stats_update_io_remove(bfqq_group(bfqq), rq->cmd_flags);
#endif
}

static int bfq_merge(struct request_queue *q, struct request **req,
		     struct bio *bio)
{
	struct bfq_data *bfqd = q->elevator->elevator_data;
	struct request *__rq;

	__rq = bfq_find_rq_fmerge(bfqd, bio);
	if (__rq && elv_rq_merge_ok(__rq, bio)) {
		*req = __rq;
		return ELEVATOR_FRONT_MERGE;
	}

	return ELEVATOR_NO_MERGE;
}

static void bfq_merged_request(struct request_queue *q, struct request *req,
			       int type)
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
		BUG_ON(!next_rq);
		bfqq->next_rq = next_rq;
	}
}

#ifdef CONFIG_BFQ_GROUP_IOSCHED
static void bfq_bio_merged(struct request_queue *q, struct request *req,
			   struct bio *bio)
{
	bfqg_stats_update_io_merged(bfqq_group(RQ_BFQQ(req)), bio->bi_rw);
}
#endif

static void bfq_merged_requests(struct request_queue *q, struct request *rq,
				struct request *next)
{
	struct bfq_queue *bfqq = RQ_BFQQ(rq), *next_bfqq = RQ_BFQQ(next);

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
	    time_before(next->fifo_time, rq->fifo_time)) {
		list_del_init(&rq->queuelist);
		list_replace_init(&next->queuelist, &rq->queuelist);
		rq->fifo_time = next->fifo_time;
	}

	if (bfqq->next_rq == next)
		bfqq->next_rq = rq;

	bfq_remove_request(next);
#ifdef CONFIG_BFQ_GROUP_IOSCHED
	bfqg_stats_update_io_merged(bfqq_group(bfqq), next->cmd_flags);
#endif
}

/* Must be called with bfqq != NULL */
static void bfq_bfqq_end_wr(struct bfq_queue *bfqq)
{
	BUG_ON(!bfqq);
	if (bfq_bfqq_busy(bfqq))
		bfqq->bfqd->wr_busy_queues--;
	bfqq->wr_coeff = 1;
	bfqq->wr_cur_max_time = 0;
	/* Trigger a weight change on the next activation of the queue */
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

	spin_lock_irq(bfqd->queue->queue_lock);

	list_for_each_entry(bfqq, &bfqd->active_list, bfqq_list)
		bfq_bfqq_end_wr(bfqq);
	list_for_each_entry(bfqq, &bfqd->idle_list, bfqq_list)
		bfq_bfqq_end_wr(bfqq);
	bfq_end_wr_async(bfqd);

	spin_unlock_irq(bfqd->queue->queue_lock);
}

static int bfq_allow_merge(struct request_queue *q, struct request *rq,
			   struct bio *bio)
{
	struct bfq_data *bfqd = q->elevator->elevator_data;
	struct bfq_io_cq *bic;

	/*
	 * Disallow merge of a sync bio into an async request.
	 */
	if (bfq_bio_sync(bio) && !rq_is_sync(rq))
		return 0;

	/*
	 * Lookup the bfqq that this bio will be queued with. Allow
	 * merge only if rq is queued there.
	 * Queue lock is held here.
	 */
	bic = bfq_bic_lookup(bfqd, current->io_context);
	if (!bic)
		return 0;

	return bic_to_bfqq(bic, bfq_bio_sync(bio)) == RQ_BFQQ(rq);
}

static void __bfq_set_in_service_queue(struct bfq_data *bfqd,
				       struct bfq_queue *bfqq)
{
	if (bfqq) {
#ifdef CONFIG_BFQ_GROUP_IOSCHED
		bfqg_stats_update_avg_queue_size(bfqq_group(bfqq));
#endif
		bfq_mark_bfqq_must_alloc(bfqq);
		bfq_mark_bfqq_budget_new(bfqq);
		bfq_clear_bfqq_fifo_expire(bfqq);

		bfqd->budgets_assigned = (bfqd->budgets_assigned*7 + 256) / 8;

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

static void bfq_arm_slice_timer(struct bfq_data *bfqd)
{
	struct bfq_queue *bfqq = bfqd->in_service_queue;
	struct bfq_io_cq *bic;
	unsigned long sl;

	BUG_ON(!RB_EMPTY_ROOT(&bfqq->sort_list));

	/* Processes have exited, don't wait. */
	bic = bfqd->in_service_bic;
	if (!bic || atomic_read(&bic->icq.ioc->active_ref) == 0)
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
	/*
	 * Unless the queue is being weight-raised or the scenario is
	 * asymmetric, grant only minimum idle time if the queue either
	 * has been seeky for long enough or has already proved to be
	 * constantly seeky.
	 */
	if (bfq_sample_valid(bfqq->seek_samples) &&
	    ((BFQQ_SEEKY(bfqq) && bfqq->entity.service >
				  bfq_max_budget(bfqq->bfqd) / 8) ||
	      bfq_bfqq_constantly_seeky(bfqq)) && bfqq->wr_coeff == 1 &&
	    bfq_symmetric_scenario(bfqd))
		sl = min(sl, msecs_to_jiffies(BFQ_MIN_TT));
	else if (bfqq->wr_coeff > 1)
		sl = sl * 3;
	bfqd->last_idling_start = ktime_get();
	mod_timer(&bfqd->idle_slice_timer, jiffies + sl);
#ifdef CONFIG_BFQ_GROUP_IOSCHED
	bfqg_stats_set_start_idle_time(bfqq_group(bfqq));
#endif
	bfq_log(bfqd, "arm idle: %u/%u ms",
		jiffies_to_msecs(sl), jiffies_to_msecs(bfqd->bfq_slice_idle));
}

/*
 * Set the maximum time for the in-service queue to consume its
 * budget. This prevents seeky processes from lowering the disk
 * throughput (always guaranteed with a time slice scheme as in CFQ).
 */
static void bfq_set_budget_timeout(struct bfq_data *bfqd)
{
	struct bfq_queue *bfqq = bfqd->in_service_queue;
	unsigned int timeout_coeff;
	if (bfqq->wr_cur_max_time == bfqd->bfq_wr_rt_max_time)
		timeout_coeff = 1;
	else
		timeout_coeff = bfqq->entity.weight / bfqq->entity.orig_weight;

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

	/*
	 * For consistency, the next instruction should have been executed
	 * after removing the request from the queue and dispatching it.
	 * We execute instead this instruction before bfq_remove_request()
	 * (and hence introduce a temporary inconsistency), for efficiency.
	 * In fact, in a forced_dispatch, this prevents two counters related
	 * to bfqq->dispatched to risk to be uselessly decremented if bfqq
	 * is not in service, and then to be incremented again after
	 * incrementing bfqq->dispatched.
	 */
	bfqq->dispatched++;
	bfq_remove_request(rq);
	elv_dispatch_sort(q, rq);

	if (bfq_bfqq_sync(bfqq))
		bfqd->sync_flight++;
#ifdef CONFIG_BFQ_GROUP_IOSCHED
	bfqg_stats_update_dispatch(bfqq_group(bfqq), blk_rq_bytes(rq),
				   rq->cmd_flags);
#endif
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

	if (time_before(jiffies, rq->fifo_time))
		return NULL;

	return rq;
}

static int bfq_bfqq_budget_left(struct bfq_queue *bfqq)
{
	struct bfq_entity *entity = &bfqq->entity;
	return entity->budget - entity->service;
}

static void __bfq_bfqq_expire(struct bfq_data *bfqd, struct bfq_queue *bfqq)
{
	BUG_ON(bfqq != bfqd->in_service_queue);

	__bfq_bfqd_reset_in_service(bfqd);

	if (RB_EMPTY_ROOT(&bfqq->sort_list)) {
		/*
		 * Overloading budget_timeout field to store the time
		 * at which the queue remains with no backlog; used by
		 * the weight-raising mechanism.
		 */
		bfqq->budget_timeout = jiffies;
		bfq_del_bfqq_busy(bfqd, bfqq, 1);
	} else
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

	BUG_ON(bfqq != bfqd->in_service_queue);

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
		case BFQ_BFQQ_TOO_IDLE:
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
	} else
		/*
		 * Async queues get always the maximum possible budget
		 * (their ability to dispatch is limited by
		 * @bfqd->bfq_max_budget_async_rq).
		 */
		budget = bfqd->bfq_max_budget;

	bfqq->max_budget = budget;

	if (bfqd->budgets_assigned >= bfq_stats_min_budgets &&
	    !bfqd->bfq_user_max_budget)
		bfqq->max_budget = min(bfqq->max_budget, bfqd->bfq_max_budget);

	/*
	 * Make sure that we have enough budget for the next request.
	 * Since the finish time of the bfqq must be kept in sync with
	 * the budget, be sure to call __bfq_bfqq_expire() after the
	 * update.
	 */
	next_rq = bfqq->next_rq;
	if (next_rq)
		bfqq->entity.budget = max_t(unsigned long, bfqq->max_budget,
					    bfq_serv_to_charge(next_rq, bfqq));
	else
		bfqq->entity.budget = bfqq->max_budget;

	bfq_log_bfqq(bfqd, bfqq, "head sect: %u, new budget %d",
			next_rq ? blk_rq_sectors(next_rq) : 0,
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
static bool bfq_update_peak_rate(struct bfq_data *bfqd, struct bfq_queue *bfqq,
				 bool compensate, enum bfqq_expiration reason)
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

	/* Don't trust short/unrealistic values. */
	if (usecs < 100 || usecs >= LONG_MAX)
		return false;

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
			if (bw == 0)
				return 0;
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
		    update) {
			int dev_type = blk_queue_nonrot(bfqd->queue);
			if (bfqd->bfq_user_max_budget == 0) {
				bfqd->bfq_max_budget =
					bfq_calc_max_budget(bfqd->peak_rate,
							    timeout);
				bfq_log(bfqd, "new max_budget=%d",
					bfqd->bfq_max_budget);
			}
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
		return false;

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
		   jiffies + bfqq->bfqd->bfq_slice_idle + 4);
}

/*
 * Return the largest-possible time instant such that, for as long as possible,
 * the current time will be lower than this time instant according to the macro
 * time_is_before_jiffies().
 */
static unsigned long bfq_infinity_from_now(unsigned long now)
{
	return now + ULONG_MAX / 2;
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
			    bool compensate,
			    enum bfqq_expiration reason)
{
	bool slow;
	BUG_ON(bfqq != bfqd->in_service_queue);

	/*
	 * Update disk peak rate for autotuning and check whether the
	 * process is slow (see bfq_update_peak_rate).
	 */
	slow = bfq_update_peak_rate(bfqd, bfqq, compensate, reason);

	/*
	 * As above explained, 'punish' slow (i.e., seeky), timed-out
	 * and async queues, to favor sequential sync workloads.
	 *
	 * Processes doing I/O in the slower disk zones will tend to be
	 * slow(er) even if not seeky. Hence, since the estimated peak
	 * rate is actually an average over the disk surface, these
	 * processes may timeout just for bad luck. To avoid punishing
	 * them we do not charge a full budget to a process that
	 * succeeded in consuming at least 2/3 of its budget.
	 */
	if (slow || (reason == BFQ_BFQQ_BUDGET_TIMEOUT &&
		     bfq_bfqq_budget_left(bfqq) >=  bfqq->entity.budget / 3))
		bfq_bfqq_charge_full_budget(bfqq);

	bfqq->service_from_backlogged += bfqq->entity.service;

	if (BFQQ_SEEKY(bfqq) && reason == BFQ_BFQQ_BUDGET_TIMEOUT &&
	    !bfq_bfqq_constantly_seeky(bfqq)) {
		bfq_mark_bfqq_constantly_seeky(bfqq);
		if (!blk_queue_nonrot(bfqd->queue))
			bfqd->const_seeky_busy_in_flight_queues++;
	}

	if (reason == BFQ_BFQQ_TOO_IDLE &&
	    bfqq->entity.service <= 2 * bfqq->entity.budget / 10 )
		bfq_clear_bfqq_IO_bound(bfqq);

	if (bfqd->low_latency && bfqq->wr_coeff == 1)
		bfqq->last_wr_start_finish = jiffies;

	if (bfqd->low_latency && bfqd->bfq_wr_max_softrt_rate > 0 &&
	    RB_EMPTY_ROOT(&bfqq->sort_list)) {
		/*
		 * If we get here, and there are no outstanding requests,
		 * then the request pattern is isochronous (see the comments
		 * to the function bfq_bfqq_softrt_next_start()). Hence we
		 * can compute soft_rt_next_start. If, instead, the queue
		 * still has outstanding requests, then we have to wait
		 * for the completion of all the outstanding requests to
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
				bfq_infinity_from_now(jiffies);
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
	__bfq_bfqq_expire(bfqd, bfqq);
}

/*
 * Budget timeout is not implemented through a dedicated timer, but
 * just checked on request arrivals and completions, as well as on
 * idle timer expirations.
 */
static bool bfq_bfqq_budget_timeout(struct bfq_queue *bfqq)
{
	if (bfq_bfqq_budget_new(bfqq) ||
	    time_before(jiffies, bfqq->budget_timeout))
		return false;
	return true;
}

/*
 * If we expire a queue that is waiting for the arrival of a new
 * request, we may prevent the fictitious timestamp back-shifting that
 * allows the guarantees of the queue to be preserved (see [1] for
 * this tricky aspect). Hence we return true only if this condition
 * does not hold, or if the queue is slow enough to deserve only to be
 * kicked off for preserving a high throughput.
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
 * this function returns true for that queue. As a consequence, since
 * device idling plays a critical role for both throughput boosting
 * and service guarantees, the return value of this function plays a
 * critical role as well.
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
 * while introducing the variables.
 */
static bool bfq_bfqq_may_idle(struct bfq_queue *bfqq)
{
	struct bfq_data *bfqd = bfqq->bfqd;
	bool idling_boosts_thr, idling_boosts_thr_without_issues,
		all_queues_seeky, on_hdd_and_not_all_queues_seeky,
		idling_needed_for_service_guarantees,
		asymmetric_scenario;

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
		 bfq_bfqq_idle_window(bfqq)) ;

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
	 * There are then two cases where idling must be performed not
	 * for throughput concerns, but to preserve service
	 * guarantees. In the description of these cases, we say, for
	 * short, that a queue is sequential/random if the process
	 * associated to the queue issues sequential/random requests
	 * (in the second case the queue may be tagged as seeky or
	 * even constantly_seeky).
	 *
	 * To introduce the first case, we note that, since
	 * bfq_bfqq_idle_window(bfqq) is false if the device is
	 * NCQ-capable and bfqq is random (see
	 * bfq_update_idle_window()), then, from the above two
	 * assignments it follows that
	 * idling_boosts_thr_without_issues is false if the device is
	 * NCQ-capable and bfqq is random. Therefore, for this case,
	 * device idling would never be allowed if we used just
	 * idling_boosts_thr_without_issues to decide whether to allow
	 * it. And, beneficially, this would imply that throughput
	 * would always be boosted also with random I/O on NCQ-capable
	 * HDDs.
	 *
	 * But we must be careful on this point, to avoid an unfair
	 * treatment for bfqq. In fact, because of the same above
	 * assignments, idling_boosts_thr_without_issues is, on the
	 * other hand, true if 1) the device is an HDD and bfqq is
	 * sequential, and 2) there are no busy weight-raised
	 * queues. As a consequence, if we used just
	 * idling_boosts_thr_without_issues to decide whether to idle
	 * the device, then with an HDD we might easily bump into a
	 * scenario where queues that are sequential and I/O-bound
	 * would enjoy idling, whereas random queues would not. The
	 * latter might then get a low share of the device throughput,
	 * simply because the former would get many requests served
	 * after being set as in service, while the latter would not.
	 *
	 * To address this issue, we start by setting to true a
	 * sentinel variable, on_hdd_and_not_all_queues_seeky, if the
	 * device is rotational and not all queues with pending or
	 * in-flight requests are constantly seeky (i.e., there are
	 * active sequential queues, and bfqq might then be mistreated
	 * if it does not enjoy idling because it is random).
	 */
	all_queues_seeky = bfq_bfqq_constantly_seeky(bfqq) &&
			   bfqd->busy_in_flight_queues ==
			   bfqd->const_seeky_busy_in_flight_queues;

	on_hdd_and_not_all_queues_seeky =
		!blk_queue_nonrot(bfqd->queue) && !all_queues_seeky;

	/*
	 * To introduce the second case where idling needs to be
	 * performed to preserve service guarantees, we can note that
	 * allowing the drive to enqueue more than one request at a
	 * time, and hence delegating de facto final scheduling
	 * decisions to the drive's internal scheduler, causes loss of
	 * control on the actual request service order. In particular,
	 * the critical situation is when requests from different
	 * processes happens to be present, at the same time, in the
	 * internal queue(s) of the drive. In such a situation, the
	 * drive, by deciding the service order of the
	 * internally-queued requests, does determine also the actual
	 * throughput distribution among these processes. But the
	 * drive typically has no notion or concern about per-process
	 * throughput distribution, and makes its decisions only on a
	 * per-request basis. Therefore, the service distribution
	 * enforced by the drive's internal scheduler is likely to
	 * coincide with the desired device-throughput distribution
	 * only in a completely symmetric scenario where:
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
	 * for not controlling also sub-condition (ii) is that, first,
	 * in the case of an HDD, the asymmetry in terms of types of
	 * I/O patterns is already taken in to account in the above
	 * sentinel variable
	 * on_hdd_and_not_all_queues_seeky. Secondly, in the case of a
	 * flash-based device, we prefer however to privilege
	 * throughput (and idling lowers throughput for this type of
	 * devices), for the following reasons:
	 * 1) differently from HDDs, the service time of random
	 *    requests is not orders of magnitudes lower than the service
	 *    time of sequential requests; thus, even if processes doing
	 *    sequential I/O get a preferential treatment with respect to
	 *    others doing random I/O, the consequences are not as
	 *    dramatic as with HDDs;
	 * 2) if a process doing random I/O does need strong
	 *    throughput guarantees, it is hopefully already being
	 *    weight-raised, or the user is likely to have assigned it a
	 *    higher weight than the other processes (and thus
	 *    sub-condition (i) is likely to be false, which triggers
	 *    idling).
	 *
	 * According to the above considerations, the next variable is
	 * true (only) if sub-condition (i) holds. To compute the
	 * value of this variable, we not only use the return value of
	 * the function bfq_symmetric_scenario(), but also check
	 * whether bfqq is being weight-raised, because
	 * bfq_symmetric_scenario() does not take into account also
	 * weight-raised queues (see comments to
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
	 * comments to bfq_handle_burst. Thus, if bfqq became active
	 * in a burst and not idling the device maximizes throughput,
	 * then the device must no be idled, because not idling the
	 * device provides bfqq and all other queues in the burst with
	 * maximum benefit. Combining this and the two cases above, we
	 * can now establish when idling is actually needed to
	 * preserve service guarantees.
	 */
	idling_needed_for_service_guarantees =
		(on_hdd_and_not_all_queues_seeky || asymmetric_scenario) &&
		!bfq_bfqq_in_large_burst(bfqq);

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
 * See the comments to the function bfq_bfqq_may_idle for the reasons
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
	enum bfqq_expiration reason = BFQ_BFQQ_BUDGET_TIMEOUT;

	bfqq = bfqd->in_service_queue;
	if (!bfqq)
		goto new_queue;

	bfq_log_bfqq(bfqd, bfqq, "select_queue: already in-service queue");

	if (bfq_may_expire_for_budg_timeout(bfqq) &&
	    !timer_pending(&bfqd->idle_slice_timer) &&
	    !bfq_bfqq_must_idle(bfqq))
		goto expire;

	next_rq = bfqq->next_rq;
	/*
	 * If bfqq has requests queued and it has enough budget left to
	 * serve them, keep the queue, otherwise expire it.
	 */
	if (next_rq) {
		if (bfq_serv_to_charge(next_rq, bfqq) >
			bfq_bfqq_budget_left(bfqq)) {
			reason = BFQ_BFQQ_BUDGET_EXHAUSTED;
			goto expire;
		} else {
			/*
			 * The idle timer may be pending because we may
			 * not disable disk idling even when a new request
			 * arrives.
			 */
			if (timer_pending(&bfqd->idle_slice_timer)) {
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
				del_timer(&bfqd->idle_slice_timer);
#ifdef CONFIG_BFQ_GROUP_IOSCHED
				bfqg_stats_update_idle_time(bfqq_group(bfqq));
#endif
			}
			goto keep_queue;
		}
	}

	/*
	 * No requests pending. However, if the in-service queue is idling
	 * for a new request, or has requests waiting for a completion and
	 * may idle after their completion, then keep it anyway.
	 */
	if (timer_pending(&bfqd->idle_slice_timer) ||
	    (bfqq->dispatched != 0 && bfq_bfqq_may_idle(bfqq))) {
		bfqq = NULL;
		goto keep_queue;
	}

	reason = BFQ_BFQQ_NO_MORE_REQUESTS;
expire:
	bfq_bfqq_expire(bfqd, bfqq, false, reason);
new_queue:
	bfqq = bfq_set_in_service_queue(bfqd);
	bfq_log(bfqd, "select_queue: new queue %d returned",
		bfqq ? bfqq->pid : 0);
keep_queue:
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

		BUG_ON(bfqq != bfqd->in_service_queue && entity->weight !=
		       entity->orig_weight * bfqq->wr_coeff);
		if (entity->prio_changed)
			bfq_log_bfqq(bfqd, bfqq, "WARN: pending prio change");

		/*
		 * If the queue was activated in a burst, or
		 * too much time has elapsed from the beginning
		 * of this weight-raising period, then end weight
		 * raising.
		 */
		if (bfq_bfqq_in_large_burst(bfqq) ||
		    time_is_before_jiffies(bfqq->last_wr_start_finish +
					   bfqq->wr_cur_max_time)) {
			bfqq->last_wr_start_finish = jiffies;
			bfq_log_bfqq(bfqd, bfqq,
				     "wrais ending at %lu, rais_max_time %u",
				     bfqq->last_wr_start_finish,
				     jiffies_to_msecs(bfqq->wr_cur_max_time));
			bfq_bfqq_end_wr(bfqq);
		}
	}
	/* Update weight both if it must be raised and if it must be lowered */
	if ((entity->weight > entity->orig_weight) != (bfqq->wr_coeff > 1))
		__bfq_entity_update_weight_prio(
			bfq_entity_service_tree(entity),
			entity);
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
	if (!rq)
		rq = bfqq->next_rq;
	service_to_charge = bfq_serv_to_charge(rq, bfqq);

	if (service_to_charge > bfq_bfqq_budget_left(bfqq)) {
		/*
		 * This may happen if the next rq is chosen in fifo order
		 * instead of sector order. The budget is properly
		 * dimensioned to be always sufficient to serve the next
		 * request only if it is chosen in sector order. The reason
		 * is that it would be quite inefficient and little useful
		 * to always make sure that the budget is large enough to
		 * serve even the possible next rq in fifo order.
		 * In fact, requests are seldom served in fifo order.
		 *
		 * Expire the queue for budget exhaustion, and make sure
		 * that the next act_budget is enough to serve the next
		 * request, even if it comes from the fifo expired path.
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

	bfq_update_wr_data(bfqd, bfqq);

	bfq_log_bfqq(bfqd, bfqq,
			"dispatched %u sec req (%llu), budg left %d",
			blk_rq_sectors(rq),
			(long long unsigned)blk_rq_pos(rq),
			bfq_bfqq_budget_left(bfqq));

	dispatched++;

	if (!bfqd->in_service_bic) {
		atomic_long_inc(&RQ_BIC(rq)->icq.ioc->refcount);
		bfqd->in_service_bic = RQ_BIC(rq);
	}

	if (bfqd->busy_queues > 1 && ((!bfq_bfqq_sync(bfqq) &&
	    dispatched >= bfqd->bfq_max_budget_async_rq) ||
	    bfq_class_idle(bfqq)))
		goto expire;

	return dispatched;

expire:
	bfq_bfqq_expire(bfqd, bfqq, false, BFQ_BFQQ_BUDGET_EXHAUSTED);
	return dispatched;
}

static int __bfq_forced_dispatch_bfqq(struct bfq_queue *bfqq)
{
	int dispatched = 0;

	while (bfqq->next_rq) {
		bfq_dispatch_insert(bfqq->bfqd->queue, bfqq->next_rq);
		dispatched++;
	}

	BUG_ON(!list_empty(&bfqq->fifo));
	return dispatched;
}

/*
 * Drain our current requests.
 * Used for barriers and when switching io schedulers on-the-fly.
 */
static int bfq_forced_dispatch(struct bfq_data *bfqd)
{
	struct bfq_queue *bfqq, *n;
	struct bfq_service_tree *st;
	int dispatched = 0;

	bfqq = bfqd->in_service_queue;
	if (bfqq)
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

	bfqq = bfq_select_queue(bfqd);
	if (!bfqq)
		return 0;

	if (bfq_class_idle(bfqq))
		max_dispatch = 1;

	if (!bfq_bfqq_sync(bfqq))
		max_dispatch = bfqd->bfq_max_budget_async_rq;

	if (!bfq_bfqq_sync(bfqq) && bfqq->dispatched >= max_dispatch) {
		if (bfqd->busy_queues > 1)
			return 0;
		if (bfqq->dispatched >= 4 * max_dispatch)
			return 0;
	}

	if (bfqd->sync_flight != 0 && !bfq_bfqq_sync(bfqq))
		return 0;

	bfq_clear_bfqq_wait_request(bfqq);
	BUG_ON(timer_pending(&bfqd->idle_slice_timer));

	if (!bfq_dispatch_request(bfqd, bfqq))
		return 0;

	bfq_log_bfqq(bfqd, bfqq, "dispatched %s request",
			bfq_bfqq_sync(bfqq) ? "sync" : "async");

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
#ifdef CONFIG_BFQ_GROUP_IOSCHED
	struct bfq_group *bfqg = bfqq_group(bfqq);
#endif

	BUG_ON(atomic_read(&bfqq->ref) <= 0);

	bfq_log_bfqq(bfqd, bfqq, "put_queue: %p %d", bfqq,
		     atomic_read(&bfqq->ref));
	if (!atomic_dec_and_test(&bfqq->ref))
		return;

	BUG_ON(rb_first(&bfqq->sort_list));
	BUG_ON(bfqq->allocated[READ] + bfqq->allocated[WRITE] != 0);
	BUG_ON(bfqq->entity.tree);
	BUG_ON(bfq_bfqq_busy(bfqq));
	BUG_ON(bfqd->in_service_queue == bfqq);

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

	bfq_log_bfqq(bfqd, bfqq, "put_queue: %p freed", bfqq);

	kmem_cache_free(bfq_pool, bfqq);
#ifdef CONFIG_BFQ_GROUP_IOSCHED
	bfqg_put(bfqg);
#endif
}

static void bfq_exit_bfqq(struct bfq_data *bfqd, struct bfq_queue *bfqq)
{
	if (bfqq == bfqd->in_service_queue) {
		__bfq_bfqq_expire(bfqd, bfqq);
		bfq_schedule_dispatch(bfqd);
	}

	bfq_log_bfqq(bfqd, bfqq, "exit_bfqq: %p, %d", bfqq,
		     atomic_read(&bfqq->ref));

	bfq_put_queue(bfqq);
}

static void bfq_init_icq(struct io_cq *icq)
{
	struct bfq_io_cq *bic = icq_to_bic(icq);

	bic->ttime.last_end_request = jiffies;
}

static void bfq_exit_icq(struct io_cq *icq)
{
	struct bfq_io_cq *bic = icq_to_bic(icq);
	struct bfq_data *bfqd = bic_to_bfqd(bic);

	if (bic->bfqq[BLK_RW_ASYNC]) {
		bfq_exit_bfqq(bfqd, bic->bfqq[BLK_RW_ASYNC]);
		bic->bfqq[BLK_RW_ASYNC] = NULL;
	}

	if (bic->bfqq[BLK_RW_SYNC]) {
		bfq_exit_bfqq(bfqd, bic->bfqq[BLK_RW_SYNC]);
		bic->bfqq[BLK_RW_SYNC] = NULL;
	}
}

/*
 * Update the entity prio values; note that the new values will not
 * be used until the next (re)activation.
 */
static void bfq_set_next_ioprio_data(struct bfq_queue *bfqq, struct bfq_io_cq *bic)
{
	struct task_struct *tsk = current;
	int ioprio_class;

	ioprio_class = IOPRIO_PRIO_CLASS(bic->ioprio);
	switch (ioprio_class) {
	default:
		dev_err(bfqq->bfqd->queue->backing_dev_info.dev,
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

	if (bfqq->new_ioprio < 0 || bfqq->new_ioprio >= IOPRIO_BE_NR) {
		printk(KERN_CRIT "bfq_set_next_ioprio_data: new_ioprio %d\n",
				 bfqq->new_ioprio);
		BUG();
	}

	bfqq->entity.new_weight = bfq_ioprio_to_weight(bfqq->new_ioprio);
	bfqq->entity.prio_changed = 1;
}

static void bfq_check_ioprio_change(struct bfq_io_cq *bic, struct bio *bio)
{
	struct bfq_data *bfqd;
	struct bfq_queue *bfqq, *new_bfqq;
	unsigned long uninitialized_var(flags);
	int ioprio = bic->icq.ioc->ioprio;

	bfqd = bfq_get_bfqd_locked(&(bic->icq.q->elevator->elevator_data),
				   &flags);
	/*
	 * This condition may trigger on a newly created bic, be sure to
	 * drop the lock before returning.
	 */
	if (unlikely(!bfqd) || likely(bic->ioprio == ioprio))
		goto out;

	bic->ioprio = ioprio;

	bfqq = bic->bfqq[BLK_RW_ASYNC];
	if (bfqq) {
		new_bfqq = bfq_get_queue(bfqd, bio, BLK_RW_ASYNC, bic,
					 GFP_ATOMIC);
		if (new_bfqq) {
			bic->bfqq[BLK_RW_ASYNC] = new_bfqq;
			bfq_log_bfqq(bfqd, bfqq,
				     "check_ioprio_change: bfqq %p %d",
				     bfqq, atomic_read(&bfqq->ref));
			bfq_put_queue(bfqq);
		}
	}

	bfqq = bic->bfqq[BLK_RW_SYNC];
	if (bfqq)
		bfq_set_next_ioprio_data(bfqq, bic);

out:
	bfq_put_bfqd_unlock(bfqd, &flags);
}

static void bfq_init_bfqq(struct bfq_data *bfqd, struct bfq_queue *bfqq,
			  struct bfq_io_cq *bic, pid_t pid, int is_sync)
{
	RB_CLEAR_NODE(&bfqq->entity.rb_node);
	INIT_LIST_HEAD(&bfqq->fifo);
	INIT_HLIST_NODE(&bfqq->burst_list_node);

	atomic_set(&bfqq->ref, 0);
	bfqq->bfqd = bfqd;

	if (bic)
		bfq_set_next_ioprio_data(bfqq, bic);

	if (is_sync) {
		if (!bfq_class_idle(bfqq))
			bfq_mark_bfqq_idle_window(bfqq);
		bfq_mark_bfqq_sync(bfqq);
	} else
		bfq_clear_bfqq_sync(bfqq);
	bfq_mark_bfqq_IO_bound(bfqq);

	/* Tentative initial value to trade off between thr and lat */
	bfqq->max_budget = (2 * bfq_max_budget(bfqd)) / 3;
	bfqq->pid = pid;

	bfqq->wr_coeff = 1;
	bfqq->last_wr_start_finish = 0;
	/*
	 * Set to the value for which bfqq will not be deemed as
	 * soft rt when it becomes backlogged.
	 */
	bfqq->soft_rt_next_start = bfq_infinity_from_now(jiffies);
}

static struct bfq_queue *bfq_find_alloc_queue(struct bfq_data *bfqd,
					      struct bio *bio, int is_sync,
					      struct bfq_io_cq *bic,
					      gfp_t gfp_mask)
{
	struct bfq_group *bfqg;
	struct bfq_queue *bfqq, *new_bfqq = NULL;
	struct blkcg *blkcg;

retry:
	rcu_read_lock();

	blkcg = bio_blkcg(bio);
	bfqg = bfq_find_alloc_group(bfqd, blkcg);
	/* bic always exists here */
	bfqq = bic_to_bfqq(bic, is_sync);

	/*
	 * Always try a new alloc if we fall back to the OOM bfqq
	 * originally, since it should just be a temporary situation.
	 */
	if (!bfqq || bfqq == &bfqd->oom_bfqq) {
		bfqq = NULL;
		if (new_bfqq) {
			bfqq = new_bfqq;
			new_bfqq = NULL;
		} else if (gfpflags_allow_blocking(gfp_mask)) {
			rcu_read_unlock();
			spin_unlock_irq(bfqd->queue->queue_lock);
			new_bfqq = kmem_cache_alloc_node(bfq_pool,
					gfp_mask | __GFP_ZERO,
					bfqd->queue->node);
			spin_lock_irq(bfqd->queue->queue_lock);
			if (new_bfqq)
				goto retry;
		} else {
			bfqq = kmem_cache_alloc_node(bfq_pool,
					gfp_mask | __GFP_ZERO,
					bfqd->queue->node);
		}

		if (bfqq) {
			bfq_init_bfqq(bfqd, bfqq, bic, current->pid,
                                      is_sync);
			bfq_init_entity(&bfqq->entity, bfqg);
			bfq_log_bfqq(bfqd, bfqq, "allocated");
		} else {
			bfqq = &bfqd->oom_bfqq;
			bfq_log_bfqq(bfqd, bfqq, "using oom bfqq");
		}
	}

	if (new_bfqq)
		kmem_cache_free(bfq_pool, new_bfqq);

	rcu_read_unlock();

	return bfqq;
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
		BUG();
	}
}

static struct bfq_queue *bfq_get_queue(struct bfq_data *bfqd,
				       struct bio *bio, int is_sync,
				       struct bfq_io_cq *bic, gfp_t gfp_mask)
{
	const int ioprio = IOPRIO_PRIO_DATA(bic->ioprio);
	const int ioprio_class = IOPRIO_PRIO_CLASS(bic->ioprio);
	struct bfq_queue **async_bfqq = NULL;
	struct bfq_queue *bfqq = NULL;

	if (!is_sync) {
		struct blkcg *blkcg;
		struct bfq_group *bfqg;

		rcu_read_lock();
		blkcg = bio_blkcg(bio);
		rcu_read_unlock();
		bfqg = bfq_find_alloc_group(bfqd, blkcg);
		async_bfqq = bfq_async_queue_prio(bfqd, bfqg, ioprio_class,
						  ioprio);
		bfqq = *async_bfqq;
	}

	if (!bfqq)
		bfqq = bfq_find_alloc_queue(bfqd, bio, is_sync, bic, gfp_mask);

	/*
	 * Pin the queue now that it's allocated, scheduler exit will
	 * prune it.
	 */
	if (!is_sync && !(*async_bfqq)) {
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
				    struct bfq_io_cq *bic)
{
	unsigned long elapsed = jiffies - bic->ttime.last_end_request;
	unsigned long ttime = min(elapsed, 2UL * bfqd->bfq_slice_idle);

	bic->ttime.ttime_samples = (7*bic->ttime.ttime_samples + 256) / 8;
	bic->ttime.ttime_total = (7*bic->ttime.ttime_total + 256*ttime) / 8;
	bic->ttime.ttime_mean = (bic->ttime.ttime_total + 128) /
				bic->ttime.ttime_samples;
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
				   struct bfq_io_cq *bic)
{
	int enable_idle;

	/* Don't idle for async or idle io prio class. */
	if (!bfq_bfqq_sync(bfqq) || bfq_class_idle(bfqq))
		return;

	enable_idle = bfq_bfqq_idle_window(bfqq);

	if (atomic_read(&bic->icq.ioc->active_ref) == 0 ||
	    bfqd->bfq_slice_idle == 0 ||
		(bfqd->hw_tag && BFQQ_SEEKY(bfqq) &&
			bfqq->wr_coeff == 1))
		enable_idle = 0;
	else if (bfq_sample_valid(bic->ttime.ttime_samples)) {
		if (bic->ttime.ttime_mean > bfqd->bfq_slice_idle &&
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

	bfq_update_io_thinktime(bfqd, bic);
	bfq_update_io_seektime(bfqd, bfqq, rq);
	if (!BFQQ_SEEKY(bfqq) && bfq_bfqq_constantly_seeky(bfqq)) {
		bfq_clear_bfqq_constantly_seeky(bfqq);
		if (!blk_queue_nonrot(bfqd->queue)) {
			BUG_ON(!bfqd->const_seeky_busy_in_flight_queues);
			bfqd->const_seeky_busy_in_flight_queues--;
		}
	}
	if (bfqq->entity.service > bfq_max_budget(bfqd) / 8 ||
	    !BFQQ_SEEKY(bfqq))
		bfq_update_idle_window(bfqd, bfqq, bic);

	bfq_log_bfqq(bfqd, bfqq,
		     "rq_enqueued: idle_window=%d (seeky %d, mean %llu)",
		     bfq_bfqq_idle_window(bfqq), BFQQ_SEEKY(bfqq),
		     (long long unsigned)bfqq->seek_mean);

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
		 * In this way, if the disk is being idled to wait for
		 * a new request from the in-service queue, we avoid
		 * unplugging the device and committing the disk to serve
		 * just a small request. On the contrary, we wait for
		 * the block layer to decide when to unplug the device:
		 * hopefully, new requests will be merged to this one
		 * quickly, then the device will be unplugged and
		 * larger requests will be dispatched.
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
		del_timer(&bfqd->idle_slice_timer);
#ifdef CONFIG_BFQ_GROUP_IOSCHED
		bfqg_stats_update_idle_time(bfqq_group(bfqq));
#endif

		/*
		 * The queue is not empty, because a new request just
		 * arrived. Hence we can safely expire the queue, in
		 * case of budget timeout, without risking that the
		 * timestamps of the queue are not updated correctly.
		 * See [1] for more details.
		 */
		if (budget_timeout)
			bfq_bfqq_expire(bfqd, bfqq, false,
					BFQ_BFQQ_BUDGET_TIMEOUT);

		/*
		 * Let the request rip immediately, or let a new queue be
		 * selected if bfqq has just been expired.
		 */
		__blk_run_queue(bfqd->queue);
	}
}

static void bfq_insert_request(struct request_queue *q, struct request *rq)
{
	struct bfq_data *bfqd = q->elevator->elevator_data;
	struct bfq_queue *bfqq = RQ_BFQQ(rq);

	assert_spin_locked(bfqd->queue->queue_lock);

	bfq_add_request(rq);

	rq->fifo_time = jiffies + bfqd->bfq_fifo_expire[rq_is_sync(rq)];
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
	bool sync = bfq_bfqq_sync(bfqq);

	bfq_log_bfqq(bfqd, bfqq, "completed one req with %u sects left (%d)",
		     blk_rq_sectors(rq), sync);

	bfq_update_hw_tag(bfqd);

	BUG_ON(!bfqd->rq_in_driver);
	BUG_ON(!bfqq->dispatched);
	bfqd->rq_in_driver--;
	bfqq->dispatched--;
#ifdef CONFIG_BFQ_GROUP_IOSCHED
	bfqg_stats_update_completion(bfqq_group(bfqq),
				     rq_start_time_ns(rq),
				     rq_io_start_time_ns(rq), rq->cmd_flags);
#endif

	if (!bfqq->dispatched && !bfq_bfqq_busy(bfqq)) {
		bfq_weights_tree_remove(bfqd, &bfqq->entity,
					&bfqd->queue_weights_tree);
		if (!blk_queue_nonrot(bfqd->queue)) {
			BUG_ON(!bfqd->busy_in_flight_queues);
			bfqd->busy_in_flight_queues--;
			if (bfq_bfqq_constantly_seeky(bfqq)) {
				BUG_ON(!bfqd->
					const_seeky_busy_in_flight_queues);
				bfqd->const_seeky_busy_in_flight_queues--;
			}
		}
	}

	if (sync) {
		bfqd->sync_flight--;
		RQ_BIC(rq)->ttime.last_end_request = jiffies;
	}

	/*
	 * If we are waiting to discover whether the request pattern of the
	 * task associated with the queue is actually isochronous, and
	 * both requisites for this condition to hold are satisfied, then
	 * compute soft_rt_next_start (see the comments to the function
	 * bfq_bfqq_softrt_next_start()).
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
		if (bfq_bfqq_budget_new(bfqq))
			bfq_set_budget_timeout(bfqd);

		if (bfq_bfqq_must_idle(bfqq)) {
			bfq_arm_slice_timer(bfqd);
			goto out;
		} else if (bfq_may_expire_for_budg_timeout(bfqq))
			bfq_bfqq_expire(bfqd, bfqq, false,
					BFQ_BFQQ_BUDGET_TIMEOUT);
		else if (RB_EMPTY_ROOT(&bfqq->sort_list) &&
			 (bfqq->dispatched == 0 ||
			  !bfq_bfqq_may_idle(bfqq)))
			bfq_bfqq_expire(bfqd, bfqq, false,
					BFQ_BFQQ_NO_MORE_REQUESTS);
	}

	if (!bfqd->rq_in_driver)
		bfq_schedule_dispatch(bfqd);

out:
	return;
}

static int __bfq_may_queue(struct bfq_queue *bfqq)
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
	struct bfq_io_cq *bic;
	struct bfq_queue *bfqq;

	/*
	 * Don't force setup of a queue from here, as a call to may_queue
	 * does not necessarily imply that a request actually will be
	 * queued. So just lookup a possibly existing queue, or return
	 * 'may queue' if that fails.
	 */
	bic = bfq_bic_lookup(bfqd, tsk->io_context);
	if (!bic)
		return ELV_MQUEUE_MAY;

	bfqq = bic_to_bfqq(bic, rw_is_sync(rw));
	if (bfqq)
		return __bfq_may_queue(bfqq);

	return ELV_MQUEUE_MAY;
}

/*
 * Queue lock held here.
 */
static void bfq_put_request(struct request *rq)
{
	struct bfq_queue *bfqq = RQ_BFQQ(rq);

	if (bfqq) {
		const int rw = rq_data_dir(rq);

		BUG_ON(!bfqq->allocated[rw]);
		bfqq->allocated[rw]--;

		rq->elv.priv[0] = NULL;
		rq->elv.priv[1] = NULL;

		bfq_log_bfqq(bfqq->bfqd, bfqq, "put_request %p, %d",
			     bfqq, atomic_read(&bfqq->ref));
		bfq_put_queue(bfqq);
	}
}

/*
 * Allocate bfq data structures associated with this request.
 */
static int bfq_set_request(struct request_queue *q, struct request *rq,
			   struct bio *bio, gfp_t gfp_mask)
{
	struct bfq_data *bfqd = q->elevator->elevator_data;
	struct bfq_io_cq *bic = icq_to_bic(rq->elv.icq);
	const int rw = rq_data_dir(rq);
	const int is_sync = rq_is_sync(rq);
	struct bfq_queue *bfqq;
	unsigned long flags;

	might_sleep_if(gfpflags_allow_blocking(gfp_mask));

	bfq_check_ioprio_change(bic, bio);

	spin_lock_irqsave(q->queue_lock, flags);

	if (!bic)
		goto queue_fail;

	bfq_bic_update_cgroup(bic, bio);

	bfqq = bic_to_bfqq(bic, is_sync);
	if (!bfqq || bfqq == &bfqd->oom_bfqq) {
		bfqq = bfq_get_queue(bfqd, bio, is_sync, bic, gfp_mask);
		bic_set_bfqq(bic, bfqq, is_sync);
		if (is_sync) {
			if (bfqd->large_burst)
				bfq_mark_bfqq_in_large_burst(bfqq);
			else
				bfq_clear_bfqq_in_large_burst(bfqq);
		}
	}

	bfqq->allocated[rw]++;
	atomic_inc(&bfqq->ref);
	bfq_log_bfqq(bfqd, bfqq, "set_request: bfqq %p, %d", bfqq,
		     atomic_read(&bfqq->ref));

	rq->elv.priv[0] = bic;
	rq->elv.priv[1] = bfqq;

	spin_unlock_irqrestore(q->queue_lock, flags);

	return 0;

queue_fail:
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
 * Handler of the expiration of the timer running if the in-service queue
 * is idling inside its time slice.
 */
static void bfq_idle_slice_timer(unsigned long data)
{
	struct bfq_data *bfqd = (struct bfq_data *)data;
	struct bfq_queue *bfqq;
	unsigned long flags;
	enum bfqq_expiration reason;

	spin_lock_irqsave(bfqd->queue->queue_lock, flags);

	bfqq = bfqd->in_service_queue;
	/*
	 * Theoretical race here: the in-service queue can be NULL or
	 * different from the queue that was idling if the timer handler
	 * spins on the queue_lock and a new request arrives for the
	 * current queue and there is a full dispatch cycle that changes
	 * the in-service queue.  This can hardly happen, but in the worst
	 * case we just expire a queue too early.
	 */
	if (bfqq) {
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
			 * because we may not disable the timer when the
			 * first request of the in-service queue arrives
			 * during disk idling.
			 */
			reason = BFQ_BFQQ_TOO_IDLE;
		else
			goto schedule_dispatch;

		bfq_bfqq_expire(bfqd, bfqq, true, reason);
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

static void __bfq_put_async_bfqq(struct bfq_data *bfqd,
					struct bfq_queue **bfqq_ptr)
{
	struct bfq_group *root_group = bfqd->root_group;
	struct bfq_queue *bfqq = *bfqq_ptr;

	bfq_log(bfqd, "put_async_bfqq: %p", bfqq);
	if (bfqq) {
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
	struct request_queue *q = bfqd->queue;
	struct bfq_queue *bfqq, *n;

	bfq_shutdown_timer_wq(bfqd);

	spin_lock_irq(q->queue_lock);

	BUG_ON(bfqd->in_service_queue);
	list_for_each_entry_safe(bfqq, n, &bfqd->idle_list, bfqq_list)
		bfq_deactivate_bfqq(bfqd, bfqq, 0);

	spin_unlock_irq(q->queue_lock);

	bfq_shutdown_timer_wq(bfqd);

	synchronize_rcu();

	BUG_ON(timer_pending(&bfqd->idle_slice_timer));

#ifdef CONFIG_BFQ_GROUP_IOSCHED
	blkcg_deactivate_policy(q, &blkcg_policy_bfq);
#else
	kfree(bfqd->root_group);
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
	for (i = 0; i < BFQ_IOPRIO_CLASSES; i++)
		root_group->sched_data.service_tree[i] = BFQ_SERVICE_TREE_INIT;
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

	/*
	 * Our fallback bfqq if bfq_find_alloc_queue() runs into OOM issues.
	 * Grab a permanent reference to it, so that the normal code flow
	 * will not attempt to free it.
	 */
	bfq_init_bfqq(bfqd, &bfqd->oom_bfqq, NULL, 1, 0);
	atomic_inc(&bfqd->oom_bfqq.ref);
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

	spin_lock_irq(q->queue_lock);
	q->elevator = eq;
	spin_unlock_irq(q->queue_lock);

	bfqd->root_group = bfq_create_group_hierarchy(bfqd, q->node);
	if (!bfqd->root_group)
		goto out_free;
	bfq_init_root_group(bfqd->root_group, bfqd);
	bfq_init_entity(&bfqd->oom_bfqq.entity, bfqd->root_group);
#ifdef CONFIG_BFQ_GROUP_IOSCHED
	bfqd->active_numerous_groups = 0;
#endif

	init_timer(&bfqd->idle_slice_timer);
	bfqd->idle_slice_timer.function = bfq_idle_slice_timer;
	bfqd->idle_slice_timer.data = (unsigned long)bfqd;

	bfqd->queue_weights_tree = RB_ROOT;
	bfqd->group_weights_tree = RB_ROOT;

	INIT_WORK(&bfqd->unplug_work, bfq_kick_queue);

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
	bfqd->bfq_class_idle_last_service = 0;
	bfqd->bfq_max_budget_async_rq = bfq_max_budget_async_rq;
	bfqd->bfq_timeout[BLK_RW_ASYNC] = bfq_timeout_async;
	bfqd->bfq_timeout[BLK_RW_SYNC] = bfq_timeout_sync;

	bfqd->bfq_requests_within_timer = 120;

	bfqd->bfq_large_burst_thresh = 11;
	bfqd->bfq_burst_interval = msecs_to_jiffies(500);

	bfqd->low_latency = true;

	bfqd->bfq_wr_coeff = 20;
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
	bfqd->busy_in_flight_queues = 0;
	bfqd->const_seeky_busy_in_flight_queues = 0;

	/*
	 * Begin by assuming, optimistically, that the device peak rate is
	 * equal to the highest reference rate.
	 */
	bfqd->RT_prod = R_fast[blk_queue_nonrot(bfqd->queue)] *
			T_fast[blk_queue_nonrot(bfqd->queue)];
	bfqd->peak_rate = R_fast[blk_queue_nonrot(bfqd->queue)];
	bfqd->device_speed = BFQ_BFQD_FAST;

	return 0;

out_free:
	kfree(bfqd);
	kobject_put(&eq->kobj);
	return -ENOMEM;
}

static void bfq_slab_kill(void)
{
	if (bfq_pool)
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
	return sprintf(page, "%d\n", var);
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

static ssize_t bfq_wr_max_time_show(struct elevator_queue *e, char *page)
{
	struct bfq_data *bfqd = e->elevator_data;
	return sprintf(page, "%d\n", bfqd->bfq_wr_max_time > 0 ?
		       jiffies_to_msecs(bfqd->bfq_wr_max_time) :
		       jiffies_to_msecs(bfq_wr_duration(bfqd)));
}

static ssize_t bfq_weights_show(struct elevator_queue *e, char *page)
{
	struct bfq_queue *bfqq;
	struct bfq_data *bfqd = e->elevator_data;
	ssize_t num_char = 0;

	num_char += sprintf(page + num_char, "Tot reqs queued %d\n\n",
			    bfqd->queued);

	spin_lock_irq(bfqd->queue->queue_lock);

	num_char += sprintf(page + num_char, "Active:\n");
	list_for_each_entry(bfqq, &bfqd->active_list, bfqq_list) {
	  num_char += sprintf(page + num_char,
			      "pid%d: weight %hu, nr_queued %d %d, dur %d/%u\n",
			      bfqq->pid,
			      bfqq->entity.weight,
			      bfqq->queued[0],
			      bfqq->queued[1],
			jiffies_to_msecs(jiffies - bfqq->last_wr_start_finish),
			jiffies_to_msecs(bfqq->wr_cur_max_time));
	}

	num_char += sprintf(page + num_char, "Idle:\n");
	list_for_each_entry(bfqq, &bfqd->idle_list, bfqq_list) {
			num_char += sprintf(page + num_char,
				"pid%d: weight %hu, dur %d/%u\n",
				bfqq->pid,
				bfqq->entity.weight,
				jiffies_to_msecs(jiffies -
					bfqq->last_wr_start_finish),
				jiffies_to_msecs(bfqq->wr_cur_max_time));
	}

	spin_unlock_irq(bfqd->queue->queue_lock);

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
SHOW_FUNCTION(bfq_fifo_expire_sync_show, bfqd->bfq_fifo_expire[1], 1);
SHOW_FUNCTION(bfq_fifo_expire_async_show, bfqd->bfq_fifo_expire[0], 1);
SHOW_FUNCTION(bfq_back_seek_max_show, bfqd->bfq_back_max, 0);
SHOW_FUNCTION(bfq_back_seek_penalty_show, bfqd->bfq_back_penalty, 0);
SHOW_FUNCTION(bfq_slice_idle_show, bfqd->bfq_slice_idle, 1);
SHOW_FUNCTION(bfq_max_budget_show, bfqd->bfq_user_max_budget, 0);
SHOW_FUNCTION(bfq_max_budget_async_rq_show,
	      bfqd->bfq_max_budget_async_rq, 0);
SHOW_FUNCTION(bfq_timeout_sync_show, bfqd->bfq_timeout[BLK_RW_SYNC], 1);
SHOW_FUNCTION(bfq_timeout_async_show, bfqd->bfq_timeout[BLK_RW_ASYNC], 1);
SHOW_FUNCTION(bfq_low_latency_show, bfqd->low_latency, 0);
SHOW_FUNCTION(bfq_wr_coeff_show, bfqd->bfq_wr_coeff, 0);
SHOW_FUNCTION(bfq_wr_rt_max_time_show, bfqd->bfq_wr_rt_max_time, 1);
SHOW_FUNCTION(bfq_wr_min_idle_time_show, bfqd->bfq_wr_min_idle_time, 1);
SHOW_FUNCTION(bfq_wr_min_inter_arr_async_show, bfqd->bfq_wr_min_inter_arr_async,
	1);
SHOW_FUNCTION(bfq_wr_max_softrt_rate_show, bfqd->bfq_wr_max_softrt_rate, 0);
#undef SHOW_FUNCTION

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
	if (__CONV)							\
		*(__PTR) = msecs_to_jiffies(__data);			\
	else								\
		*(__PTR) = __data;					\
	return ret;							\
}
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
STORE_FUNCTION(bfq_wr_coeff_store, &bfqd->bfq_wr_coeff, 1, INT_MAX, 0);
STORE_FUNCTION(bfq_wr_max_time_store, &bfqd->bfq_wr_max_time, 0, INT_MAX, 1);
STORE_FUNCTION(bfq_wr_rt_max_time_store, &bfqd->bfq_wr_rt_max_time, 0, INT_MAX,
		1);
STORE_FUNCTION(bfq_wr_min_idle_time_store, &bfqd->bfq_wr_min_idle_time, 0,
		INT_MAX, 1);
STORE_FUNCTION(bfq_wr_min_inter_arr_async_store,
		&bfqd->bfq_wr_min_inter_arr_async, 0, INT_MAX, 1);
STORE_FUNCTION(bfq_wr_max_softrt_rate_store, &bfqd->bfq_wr_max_softrt_rate, 0,
		INT_MAX, 0);
#undef STORE_FUNCTION

/* do nothing for the moment */
static ssize_t bfq_weights_store(struct elevator_queue *e,
				    const char *page, size_t count)
{
	return count;
}

static unsigned long bfq_estimated_max_budget(struct bfq_data *bfqd)
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

	bfqd->bfq_timeout[BLK_RW_SYNC] = msecs_to_jiffies(__data);
	if (bfqd->bfq_user_max_budget == 0)
		bfqd->bfq_max_budget = bfq_estimated_max_budget(bfqd);

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
	__ATTR(name, S_IRUGO|S_IWUSR, bfq_##name##_show, bfq_##name##_store)

static struct elv_fs_entry bfq_attrs[] = {
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
	BFQ_ATTR(wr_coeff),
	BFQ_ATTR(wr_max_time),
	BFQ_ATTR(wr_rt_max_time),
	BFQ_ATTR(wr_min_idle_time),
	BFQ_ATTR(wr_min_inter_arr_async),
	BFQ_ATTR(wr_max_softrt_rate),
	BFQ_ATTR(weights),
	__ATTR_NULL
};

static struct elevator_type iosched_bfq = {
	.ops = {
		.elevator_merge_fn =		bfq_merge,
		.elevator_merged_fn =		bfq_merged_request,
		.elevator_merge_req_fn =	bfq_merged_requests,
#ifdef CONFIG_BFQ_GROUP_IOSCHED
		.elevator_bio_merged_fn =	bfq_bio_merged,
#endif
		.elevator_allow_merge_fn =	bfq_allow_merge,
		.elevator_dispatch_fn =		bfq_dispatch_requests,
		.elevator_add_req_fn =		bfq_insert_request,
		.elevator_activate_req_fn =	bfq_activate_request,
		.elevator_deactivate_req_fn =	bfq_deactivate_request,
		.elevator_completed_req_fn =	bfq_completed_request,
		.elevator_former_req_fn =	elv_rb_former_request,
		.elevator_latter_req_fn =	elv_rb_latter_request,
		.elevator_init_icq_fn =		bfq_init_icq,
		.elevator_exit_icq_fn =		bfq_exit_icq,
		.elevator_set_req_fn =		bfq_set_request,
		.elevator_put_req_fn =		bfq_put_request,
		.elevator_may_queue_fn =	bfq_may_queue,
		.elevator_init_fn =		bfq_init_queue,
		.elevator_exit_fn =		bfq_exit_queue,
	},
	.icq_size =		sizeof(struct bfq_io_cq),
	.icq_align =		__alignof__(struct bfq_io_cq),
	.elevator_attrs =	bfq_attrs,
	.elevator_name =	"bfq",
	.elevator_owner =	THIS_MODULE,
};

static int __init bfq_init(void)
{
	int ret;

	/*
	 * Can be 0 on HZ < 1000 setups.
	 */
	if (bfq_slice_idle == 0)
		bfq_slice_idle = 1;

	if (bfq_timeout_async == 0)
		bfq_timeout_async = 1;

#ifdef CONFIG_BFQ_GROUP_IOSCHED
	ret = blkcg_policy_register(&blkcg_policy_bfq);
	if (ret)
		return ret;
#endif

	ret = -ENOMEM;
	if (bfq_slab_setup())
		goto err_pol_unreg;

	/*
	 * Times to load large popular applications for the typical systems
	 * installed on the reference devices (see the comments before the
	 * definitions of the two arrays).
	 */
	T_slow[0] = msecs_to_jiffies(2600);
	T_slow[1] = msecs_to_jiffies(1000);
	T_fast[0] = msecs_to_jiffies(5500);
	T_fast[1] = msecs_to_jiffies(2000);

	/*
	 * Thresholds that determine the switch between speed classes (see
	 * the comments before the definition of the array).
	 */
	device_speed_thresh[0] = (R_fast[0] + R_slow[0]) / 2;
	device_speed_thresh[1] = (R_fast[1] + R_slow[1]) / 2;

	ret = elv_register(&iosched_bfq);
	if (ret)
		goto err_pol_unreg;

	pr_info("BFQ I/O-scheduler: v7r11");

	return 0;

err_pol_unreg:
#ifdef CONFIG_BFQ_GROUP_IOSCHED
	blkcg_policy_unregister(&blkcg_policy_bfq);
#endif
	return ret;
}

static void __exit bfq_exit(void)
{
	elv_unregister(&iosched_bfq);
#ifdef CONFIG_BFQ_GROUP_IOSCHED
	blkcg_policy_unregister(&blkcg_policy_bfq);
#endif
	bfq_slab_kill();
}

module_init(bfq_init);
module_exit(bfq_exit);

MODULE_AUTHOR("Arianna Avanzini, Fabio Checconi, Paolo Valente");
MODULE_LICENSE("GPL");
