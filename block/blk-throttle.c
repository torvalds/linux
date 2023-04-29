// SPDX-License-Identifier: GPL-2.0
/*
 * Interface for controlling IO bandwidth on a request queue
 *
 * Copyright (C) 2010 Vivek Goyal <vgoyal@redhat.com>
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/blkdev.h>
#include <linux/bio.h>
#include <linux/blktrace_api.h>
#include "blk.h"
#include "blk-cgroup-rwstat.h"
#include "blk-stat.h"
#include "blk-throttle.h"

/* Max dispatch from a group in 1 round */
#define THROTL_GRP_QUANTUM 8

/* Total max dispatch from all groups in one round */
#define THROTL_QUANTUM 32

/* Throttling is performed over a slice and after that slice is renewed */
#define DFL_THROTL_SLICE_HD (HZ / 10)
#define DFL_THROTL_SLICE_SSD (HZ / 50)
#define MAX_THROTL_SLICE (HZ)
#define MAX_IDLE_TIME (5L * 1000 * 1000) /* 5 s */
#define MIN_THROTL_BPS (320 * 1024)
#define MIN_THROTL_IOPS (10)
#define DFL_LATENCY_TARGET (-1L)
#define DFL_IDLE_THRESHOLD (0)
#define DFL_HD_BASELINE_LATENCY (4000L) /* 4ms */
#define LATENCY_FILTERED_SSD (0)
/*
 * For HD, very small latency comes from sequential IO. Such IO is helpless to
 * help determine if its IO is impacted by others, hence we ignore the IO
 */
#define LATENCY_FILTERED_HD (1000L) /* 1ms */

/* A workqueue to queue throttle related work */
static struct workqueue_struct *kthrotld_workqueue;

#define rb_entry_tg(node)	rb_entry((node), struct throtl_grp, rb_node)

/* We measure latency for request size from <= 4k to >= 1M */
#define LATENCY_BUCKET_SIZE 9

struct latency_bucket {
	unsigned long total_latency; /* ns / 1024 */
	int samples;
};

struct avg_latency_bucket {
	unsigned long latency; /* ns / 1024 */
	bool valid;
};

struct throtl_data
{
	/* service tree for active throtl groups */
	struct throtl_service_queue service_queue;

	struct request_queue *queue;

	/* Total Number of queued bios on READ and WRITE lists */
	unsigned int nr_queued[2];

	unsigned int throtl_slice;

	/* Work for dispatching throttled bios */
	struct work_struct dispatch_work;
	unsigned int limit_index;
	bool limit_valid[LIMIT_CNT];

	unsigned long low_upgrade_time;
	unsigned long low_downgrade_time;

	unsigned int scale;

	struct latency_bucket tmp_buckets[2][LATENCY_BUCKET_SIZE];
	struct avg_latency_bucket avg_buckets[2][LATENCY_BUCKET_SIZE];
	struct latency_bucket __percpu *latency_buckets[2];
	unsigned long last_calculate_time;
	unsigned long filtered_latency;

	bool track_bio_latency;
};

static void throtl_pending_timer_fn(struct timer_list *t);

static inline struct blkcg_gq *tg_to_blkg(struct throtl_grp *tg)
{
	return pd_to_blkg(&tg->pd);
}

/**
 * sq_to_tg - return the throl_grp the specified service queue belongs to
 * @sq: the throtl_service_queue of interest
 *
 * Return the throtl_grp @sq belongs to.  If @sq is the top-level one
 * embedded in throtl_data, %NULL is returned.
 */
static struct throtl_grp *sq_to_tg(struct throtl_service_queue *sq)
{
	if (sq && sq->parent_sq)
		return container_of(sq, struct throtl_grp, service_queue);
	else
		return NULL;
}

/**
 * sq_to_td - return throtl_data the specified service queue belongs to
 * @sq: the throtl_service_queue of interest
 *
 * A service_queue can be embedded in either a throtl_grp or throtl_data.
 * Determine the associated throtl_data accordingly and return it.
 */
static struct throtl_data *sq_to_td(struct throtl_service_queue *sq)
{
	struct throtl_grp *tg = sq_to_tg(sq);

	if (tg)
		return tg->td;
	else
		return container_of(sq, struct throtl_data, service_queue);
}

/*
 * cgroup's limit in LIMIT_MAX is scaled if low limit is set. This scale is to
 * make the IO dispatch more smooth.
 * Scale up: linearly scale up according to elapsed time since upgrade. For
 *           every throtl_slice, the limit scales up 1/2 .low limit till the
 *           limit hits .max limit
 * Scale down: exponentially scale down if a cgroup doesn't hit its .low limit
 */
static uint64_t throtl_adjusted_limit(uint64_t low, struct throtl_data *td)
{
	/* arbitrary value to avoid too big scale */
	if (td->scale < 4096 && time_after_eq(jiffies,
	    td->low_upgrade_time + td->scale * td->throtl_slice))
		td->scale = (jiffies - td->low_upgrade_time) / td->throtl_slice;

	return low + (low >> 1) * td->scale;
}

static uint64_t tg_bps_limit(struct throtl_grp *tg, int rw)
{
	struct blkcg_gq *blkg = tg_to_blkg(tg);
	struct throtl_data *td;
	uint64_t ret;

	if (cgroup_subsys_on_dfl(io_cgrp_subsys) && !blkg->parent)
		return U64_MAX;

	td = tg->td;
	ret = tg->bps[rw][td->limit_index];
	if (ret == 0 && td->limit_index == LIMIT_LOW) {
		/* intermediate node or iops isn't 0 */
		if (!list_empty(&blkg->blkcg->css.children) ||
		    tg->iops[rw][td->limit_index])
			return U64_MAX;
		else
			return MIN_THROTL_BPS;
	}

	if (td->limit_index == LIMIT_MAX && tg->bps[rw][LIMIT_LOW] &&
	    tg->bps[rw][LIMIT_LOW] != tg->bps[rw][LIMIT_MAX]) {
		uint64_t adjusted;

		adjusted = throtl_adjusted_limit(tg->bps[rw][LIMIT_LOW], td);
		ret = min(tg->bps[rw][LIMIT_MAX], adjusted);
	}
	return ret;
}

static unsigned int tg_iops_limit(struct throtl_grp *tg, int rw)
{
	struct blkcg_gq *blkg = tg_to_blkg(tg);
	struct throtl_data *td;
	unsigned int ret;

	if (cgroup_subsys_on_dfl(io_cgrp_subsys) && !blkg->parent)
		return UINT_MAX;

	td = tg->td;
	ret = tg->iops[rw][td->limit_index];
	if (ret == 0 && tg->td->limit_index == LIMIT_LOW) {
		/* intermediate node or bps isn't 0 */
		if (!list_empty(&blkg->blkcg->css.children) ||
		    tg->bps[rw][td->limit_index])
			return UINT_MAX;
		else
			return MIN_THROTL_IOPS;
	}

	if (td->limit_index == LIMIT_MAX && tg->iops[rw][LIMIT_LOW] &&
	    tg->iops[rw][LIMIT_LOW] != tg->iops[rw][LIMIT_MAX]) {
		uint64_t adjusted;

		adjusted = throtl_adjusted_limit(tg->iops[rw][LIMIT_LOW], td);
		if (adjusted > UINT_MAX)
			adjusted = UINT_MAX;
		ret = min_t(unsigned int, tg->iops[rw][LIMIT_MAX], adjusted);
	}
	return ret;
}

#define request_bucket_index(sectors) \
	clamp_t(int, order_base_2(sectors) - 3, 0, LATENCY_BUCKET_SIZE - 1)

/**
 * throtl_log - log debug message via blktrace
 * @sq: the service_queue being reported
 * @fmt: printf format string
 * @args: printf args
 *
 * The messages are prefixed with "throtl BLKG_NAME" if @sq belongs to a
 * throtl_grp; otherwise, just "throtl".
 */
#define throtl_log(sq, fmt, args...)	do {				\
	struct throtl_grp *__tg = sq_to_tg((sq));			\
	struct throtl_data *__td = sq_to_td((sq));			\
									\
	(void)__td;							\
	if (likely(!blk_trace_note_message_enabled(__td->queue)))	\
		break;							\
	if ((__tg)) {							\
		blk_add_cgroup_trace_msg(__td->queue,			\
			&tg_to_blkg(__tg)->blkcg->css, "throtl " fmt, ##args);\
	} else {							\
		blk_add_trace_msg(__td->queue, "throtl " fmt, ##args);	\
	}								\
} while (0)

static inline unsigned int throtl_bio_data_size(struct bio *bio)
{
	/* assume it's one sector */
	if (unlikely(bio_op(bio) == REQ_OP_DISCARD))
		return 512;
	return bio->bi_iter.bi_size;
}

static void throtl_qnode_init(struct throtl_qnode *qn, struct throtl_grp *tg)
{
	INIT_LIST_HEAD(&qn->node);
	bio_list_init(&qn->bios);
	qn->tg = tg;
}

/**
 * throtl_qnode_add_bio - add a bio to a throtl_qnode and activate it
 * @bio: bio being added
 * @qn: qnode to add bio to
 * @queued: the service_queue->queued[] list @qn belongs to
 *
 * Add @bio to @qn and put @qn on @queued if it's not already on.
 * @qn->tg's reference count is bumped when @qn is activated.  See the
 * comment on top of throtl_qnode definition for details.
 */
static void throtl_qnode_add_bio(struct bio *bio, struct throtl_qnode *qn,
				 struct list_head *queued)
{
	bio_list_add(&qn->bios, bio);
	if (list_empty(&qn->node)) {
		list_add_tail(&qn->node, queued);
		blkg_get(tg_to_blkg(qn->tg));
	}
}

/**
 * throtl_peek_queued - peek the first bio on a qnode list
 * @queued: the qnode list to peek
 */
static struct bio *throtl_peek_queued(struct list_head *queued)
{
	struct throtl_qnode *qn;
	struct bio *bio;

	if (list_empty(queued))
		return NULL;

	qn = list_first_entry(queued, struct throtl_qnode, node);
	bio = bio_list_peek(&qn->bios);
	WARN_ON_ONCE(!bio);
	return bio;
}

/**
 * throtl_pop_queued - pop the first bio form a qnode list
 * @queued: the qnode list to pop a bio from
 * @tg_to_put: optional out argument for throtl_grp to put
 *
 * Pop the first bio from the qnode list @queued.  After popping, the first
 * qnode is removed from @queued if empty or moved to the end of @queued so
 * that the popping order is round-robin.
 *
 * When the first qnode is removed, its associated throtl_grp should be put
 * too.  If @tg_to_put is NULL, this function automatically puts it;
 * otherwise, *@tg_to_put is set to the throtl_grp to put and the caller is
 * responsible for putting it.
 */
static struct bio *throtl_pop_queued(struct list_head *queued,
				     struct throtl_grp **tg_to_put)
{
	struct throtl_qnode *qn;
	struct bio *bio;

	if (list_empty(queued))
		return NULL;

	qn = list_first_entry(queued, struct throtl_qnode, node);
	bio = bio_list_pop(&qn->bios);
	WARN_ON_ONCE(!bio);

	if (bio_list_empty(&qn->bios)) {
		list_del_init(&qn->node);
		if (tg_to_put)
			*tg_to_put = qn->tg;
		else
			blkg_put(tg_to_blkg(qn->tg));
	} else {
		list_move_tail(&qn->node, queued);
	}

	return bio;
}

/* init a service_queue, assumes the caller zeroed it */
static void throtl_service_queue_init(struct throtl_service_queue *sq)
{
	INIT_LIST_HEAD(&sq->queued[READ]);
	INIT_LIST_HEAD(&sq->queued[WRITE]);
	sq->pending_tree = RB_ROOT_CACHED;
	timer_setup(&sq->pending_timer, throtl_pending_timer_fn, 0);
}

static struct blkg_policy_data *throtl_pd_alloc(struct gendisk *disk,
		struct blkcg *blkcg, gfp_t gfp)
{
	struct throtl_grp *tg;
	int rw;

	tg = kzalloc_node(sizeof(*tg), gfp, disk->node_id);
	if (!tg)
		return NULL;

	if (blkg_rwstat_init(&tg->stat_bytes, gfp))
		goto err_free_tg;

	if (blkg_rwstat_init(&tg->stat_ios, gfp))
		goto err_exit_stat_bytes;

	throtl_service_queue_init(&tg->service_queue);

	for (rw = READ; rw <= WRITE; rw++) {
		throtl_qnode_init(&tg->qnode_on_self[rw], tg);
		throtl_qnode_init(&tg->qnode_on_parent[rw], tg);
	}

	RB_CLEAR_NODE(&tg->rb_node);
	tg->bps[READ][LIMIT_MAX] = U64_MAX;
	tg->bps[WRITE][LIMIT_MAX] = U64_MAX;
	tg->iops[READ][LIMIT_MAX] = UINT_MAX;
	tg->iops[WRITE][LIMIT_MAX] = UINT_MAX;
	tg->bps_conf[READ][LIMIT_MAX] = U64_MAX;
	tg->bps_conf[WRITE][LIMIT_MAX] = U64_MAX;
	tg->iops_conf[READ][LIMIT_MAX] = UINT_MAX;
	tg->iops_conf[WRITE][LIMIT_MAX] = UINT_MAX;
	/* LIMIT_LOW will have default value 0 */

	tg->latency_target = DFL_LATENCY_TARGET;
	tg->latency_target_conf = DFL_LATENCY_TARGET;
	tg->idletime_threshold = DFL_IDLE_THRESHOLD;
	tg->idletime_threshold_conf = DFL_IDLE_THRESHOLD;

	return &tg->pd;

err_exit_stat_bytes:
	blkg_rwstat_exit(&tg->stat_bytes);
err_free_tg:
	kfree(tg);
	return NULL;
}

static void throtl_pd_init(struct blkg_policy_data *pd)
{
	struct throtl_grp *tg = pd_to_tg(pd);
	struct blkcg_gq *blkg = tg_to_blkg(tg);
	struct throtl_data *td = blkg->q->td;
	struct throtl_service_queue *sq = &tg->service_queue;

	/*
	 * If on the default hierarchy, we switch to properly hierarchical
	 * behavior where limits on a given throtl_grp are applied to the
	 * whole subtree rather than just the group itself.  e.g. If 16M
	 * read_bps limit is set on a parent group, summary bps of
	 * parent group and its subtree groups can't exceed 16M for the
	 * device.
	 *
	 * If not on the default hierarchy, the broken flat hierarchy
	 * behavior is retained where all throtl_grps are treated as if
	 * they're all separate root groups right below throtl_data.
	 * Limits of a group don't interact with limits of other groups
	 * regardless of the position of the group in the hierarchy.
	 */
	sq->parent_sq = &td->service_queue;
	if (cgroup_subsys_on_dfl(io_cgrp_subsys) && blkg->parent)
		sq->parent_sq = &blkg_to_tg(blkg->parent)->service_queue;
	tg->td = td;
}

/*
 * Set has_rules[] if @tg or any of its parents have limits configured.
 * This doesn't require walking up to the top of the hierarchy as the
 * parent's has_rules[] is guaranteed to be correct.
 */
static void tg_update_has_rules(struct throtl_grp *tg)
{
	struct throtl_grp *parent_tg = sq_to_tg(tg->service_queue.parent_sq);
	struct throtl_data *td = tg->td;
	int rw;

	for (rw = READ; rw <= WRITE; rw++) {
		tg->has_rules_iops[rw] =
			(parent_tg && parent_tg->has_rules_iops[rw]) ||
			(td->limit_valid[td->limit_index] &&
			  tg_iops_limit(tg, rw) != UINT_MAX);
		tg->has_rules_bps[rw] =
			(parent_tg && parent_tg->has_rules_bps[rw]) ||
			(td->limit_valid[td->limit_index] &&
			 (tg_bps_limit(tg, rw) != U64_MAX));
	}
}

static void throtl_pd_online(struct blkg_policy_data *pd)
{
	struct throtl_grp *tg = pd_to_tg(pd);
	/*
	 * We don't want new groups to escape the limits of its ancestors.
	 * Update has_rules[] after a new group is brought online.
	 */
	tg_update_has_rules(tg);
}

#ifdef CONFIG_BLK_DEV_THROTTLING_LOW
static void blk_throtl_update_limit_valid(struct throtl_data *td)
{
	struct cgroup_subsys_state *pos_css;
	struct blkcg_gq *blkg;
	bool low_valid = false;

	rcu_read_lock();
	blkg_for_each_descendant_post(blkg, pos_css, td->queue->root_blkg) {
		struct throtl_grp *tg = blkg_to_tg(blkg);

		if (tg->bps[READ][LIMIT_LOW] || tg->bps[WRITE][LIMIT_LOW] ||
		    tg->iops[READ][LIMIT_LOW] || tg->iops[WRITE][LIMIT_LOW]) {
			low_valid = true;
			break;
		}
	}
	rcu_read_unlock();

	td->limit_valid[LIMIT_LOW] = low_valid;
}
#else
static inline void blk_throtl_update_limit_valid(struct throtl_data *td)
{
}
#endif

static void throtl_upgrade_state(struct throtl_data *td);
static void throtl_pd_offline(struct blkg_policy_data *pd)
{
	struct throtl_grp *tg = pd_to_tg(pd);

	tg->bps[READ][LIMIT_LOW] = 0;
	tg->bps[WRITE][LIMIT_LOW] = 0;
	tg->iops[READ][LIMIT_LOW] = 0;
	tg->iops[WRITE][LIMIT_LOW] = 0;

	blk_throtl_update_limit_valid(tg->td);

	if (!tg->td->limit_valid[tg->td->limit_index])
		throtl_upgrade_state(tg->td);
}

static void throtl_pd_free(struct blkg_policy_data *pd)
{
	struct throtl_grp *tg = pd_to_tg(pd);

	del_timer_sync(&tg->service_queue.pending_timer);
	blkg_rwstat_exit(&tg->stat_bytes);
	blkg_rwstat_exit(&tg->stat_ios);
	kfree(tg);
}

static struct throtl_grp *
throtl_rb_first(struct throtl_service_queue *parent_sq)
{
	struct rb_node *n;

	n = rb_first_cached(&parent_sq->pending_tree);
	WARN_ON_ONCE(!n);
	if (!n)
		return NULL;
	return rb_entry_tg(n);
}

static void throtl_rb_erase(struct rb_node *n,
			    struct throtl_service_queue *parent_sq)
{
	rb_erase_cached(n, &parent_sq->pending_tree);
	RB_CLEAR_NODE(n);
}

static void update_min_dispatch_time(struct throtl_service_queue *parent_sq)
{
	struct throtl_grp *tg;

	tg = throtl_rb_first(parent_sq);
	if (!tg)
		return;

	parent_sq->first_pending_disptime = tg->disptime;
}

static void tg_service_queue_add(struct throtl_grp *tg)
{
	struct throtl_service_queue *parent_sq = tg->service_queue.parent_sq;
	struct rb_node **node = &parent_sq->pending_tree.rb_root.rb_node;
	struct rb_node *parent = NULL;
	struct throtl_grp *__tg;
	unsigned long key = tg->disptime;
	bool leftmost = true;

	while (*node != NULL) {
		parent = *node;
		__tg = rb_entry_tg(parent);

		if (time_before(key, __tg->disptime))
			node = &parent->rb_left;
		else {
			node = &parent->rb_right;
			leftmost = false;
		}
	}

	rb_link_node(&tg->rb_node, parent, node);
	rb_insert_color_cached(&tg->rb_node, &parent_sq->pending_tree,
			       leftmost);
}

static void throtl_enqueue_tg(struct throtl_grp *tg)
{
	if (!(tg->flags & THROTL_TG_PENDING)) {
		tg_service_queue_add(tg);
		tg->flags |= THROTL_TG_PENDING;
		tg->service_queue.parent_sq->nr_pending++;
	}
}

static void throtl_dequeue_tg(struct throtl_grp *tg)
{
	if (tg->flags & THROTL_TG_PENDING) {
		struct throtl_service_queue *parent_sq =
			tg->service_queue.parent_sq;

		throtl_rb_erase(&tg->rb_node, parent_sq);
		--parent_sq->nr_pending;
		tg->flags &= ~THROTL_TG_PENDING;
	}
}

/* Call with queue lock held */
static void throtl_schedule_pending_timer(struct throtl_service_queue *sq,
					  unsigned long expires)
{
	unsigned long max_expire = jiffies + 8 * sq_to_td(sq)->throtl_slice;

	/*
	 * Since we are adjusting the throttle limit dynamically, the sleep
	 * time calculated according to previous limit might be invalid. It's
	 * possible the cgroup sleep time is very long and no other cgroups
	 * have IO running so notify the limit changes. Make sure the cgroup
	 * doesn't sleep too long to avoid the missed notification.
	 */
	if (time_after(expires, max_expire))
		expires = max_expire;
	mod_timer(&sq->pending_timer, expires);
	throtl_log(sq, "schedule timer. delay=%lu jiffies=%lu",
		   expires - jiffies, jiffies);
}

/**
 * throtl_schedule_next_dispatch - schedule the next dispatch cycle
 * @sq: the service_queue to schedule dispatch for
 * @force: force scheduling
 *
 * Arm @sq->pending_timer so that the next dispatch cycle starts on the
 * dispatch time of the first pending child.  Returns %true if either timer
 * is armed or there's no pending child left.  %false if the current
 * dispatch window is still open and the caller should continue
 * dispatching.
 *
 * If @force is %true, the dispatch timer is always scheduled and this
 * function is guaranteed to return %true.  This is to be used when the
 * caller can't dispatch itself and needs to invoke pending_timer
 * unconditionally.  Note that forced scheduling is likely to induce short
 * delay before dispatch starts even if @sq->first_pending_disptime is not
 * in the future and thus shouldn't be used in hot paths.
 */
static bool throtl_schedule_next_dispatch(struct throtl_service_queue *sq,
					  bool force)
{
	/* any pending children left? */
	if (!sq->nr_pending)
		return true;

	update_min_dispatch_time(sq);

	/* is the next dispatch time in the future? */
	if (force || time_after(sq->first_pending_disptime, jiffies)) {
		throtl_schedule_pending_timer(sq, sq->first_pending_disptime);
		return true;
	}

	/* tell the caller to continue dispatching */
	return false;
}

static inline void throtl_start_new_slice_with_credit(struct throtl_grp *tg,
		bool rw, unsigned long start)
{
	tg->bytes_disp[rw] = 0;
	tg->io_disp[rw] = 0;
	tg->carryover_bytes[rw] = 0;
	tg->carryover_ios[rw] = 0;

	/*
	 * Previous slice has expired. We must have trimmed it after last
	 * bio dispatch. That means since start of last slice, we never used
	 * that bandwidth. Do try to make use of that bandwidth while giving
	 * credit.
	 */
	if (time_after(start, tg->slice_start[rw]))
		tg->slice_start[rw] = start;

	tg->slice_end[rw] = jiffies + tg->td->throtl_slice;
	throtl_log(&tg->service_queue,
		   "[%c] new slice with credit start=%lu end=%lu jiffies=%lu",
		   rw == READ ? 'R' : 'W', tg->slice_start[rw],
		   tg->slice_end[rw], jiffies);
}

static inline void throtl_start_new_slice(struct throtl_grp *tg, bool rw,
					  bool clear_carryover)
{
	tg->bytes_disp[rw] = 0;
	tg->io_disp[rw] = 0;
	tg->slice_start[rw] = jiffies;
	tg->slice_end[rw] = jiffies + tg->td->throtl_slice;
	if (clear_carryover) {
		tg->carryover_bytes[rw] = 0;
		tg->carryover_ios[rw] = 0;
	}

	throtl_log(&tg->service_queue,
		   "[%c] new slice start=%lu end=%lu jiffies=%lu",
		   rw == READ ? 'R' : 'W', tg->slice_start[rw],
		   tg->slice_end[rw], jiffies);
}

static inline void throtl_set_slice_end(struct throtl_grp *tg, bool rw,
					unsigned long jiffy_end)
{
	tg->slice_end[rw] = roundup(jiffy_end, tg->td->throtl_slice);
}

static inline void throtl_extend_slice(struct throtl_grp *tg, bool rw,
				       unsigned long jiffy_end)
{
	throtl_set_slice_end(tg, rw, jiffy_end);
	throtl_log(&tg->service_queue,
		   "[%c] extend slice start=%lu end=%lu jiffies=%lu",
		   rw == READ ? 'R' : 'W', tg->slice_start[rw],
		   tg->slice_end[rw], jiffies);
}

/* Determine if previously allocated or extended slice is complete or not */
static bool throtl_slice_used(struct throtl_grp *tg, bool rw)
{
	if (time_in_range(jiffies, tg->slice_start[rw], tg->slice_end[rw]))
		return false;

	return true;
}

/* Trim the used slices and adjust slice start accordingly */
static inline void throtl_trim_slice(struct throtl_grp *tg, bool rw)
{
	unsigned long nr_slices, time_elapsed, io_trim;
	u64 bytes_trim, tmp;

	BUG_ON(time_before(tg->slice_end[rw], tg->slice_start[rw]));

	/*
	 * If bps are unlimited (-1), then time slice don't get
	 * renewed. Don't try to trim the slice if slice is used. A new
	 * slice will start when appropriate.
	 */
	if (throtl_slice_used(tg, rw))
		return;

	/*
	 * A bio has been dispatched. Also adjust slice_end. It might happen
	 * that initially cgroup limit was very low resulting in high
	 * slice_end, but later limit was bumped up and bio was dispatched
	 * sooner, then we need to reduce slice_end. A high bogus slice_end
	 * is bad because it does not allow new slice to start.
	 */

	throtl_set_slice_end(tg, rw, jiffies + tg->td->throtl_slice);

	time_elapsed = jiffies - tg->slice_start[rw];

	nr_slices = time_elapsed / tg->td->throtl_slice;

	if (!nr_slices)
		return;
	tmp = tg_bps_limit(tg, rw) * tg->td->throtl_slice * nr_slices;
	do_div(tmp, HZ);
	bytes_trim = tmp;

	io_trim = (tg_iops_limit(tg, rw) * tg->td->throtl_slice * nr_slices) /
		HZ;

	if (!bytes_trim && !io_trim)
		return;

	if (tg->bytes_disp[rw] >= bytes_trim)
		tg->bytes_disp[rw] -= bytes_trim;
	else
		tg->bytes_disp[rw] = 0;

	if (tg->io_disp[rw] >= io_trim)
		tg->io_disp[rw] -= io_trim;
	else
		tg->io_disp[rw] = 0;

	tg->slice_start[rw] += nr_slices * tg->td->throtl_slice;

	throtl_log(&tg->service_queue,
		   "[%c] trim slice nr=%lu bytes=%llu io=%lu start=%lu end=%lu jiffies=%lu",
		   rw == READ ? 'R' : 'W', nr_slices, bytes_trim, io_trim,
		   tg->slice_start[rw], tg->slice_end[rw], jiffies);
}

static unsigned int calculate_io_allowed(u32 iops_limit,
					 unsigned long jiffy_elapsed)
{
	unsigned int io_allowed;
	u64 tmp;

	/*
	 * jiffy_elapsed should not be a big value as minimum iops can be
	 * 1 then at max jiffy elapsed should be equivalent of 1 second as we
	 * will allow dispatch after 1 second and after that slice should
	 * have been trimmed.
	 */

	tmp = (u64)iops_limit * jiffy_elapsed;
	do_div(tmp, HZ);

	if (tmp > UINT_MAX)
		io_allowed = UINT_MAX;
	else
		io_allowed = tmp;

	return io_allowed;
}

static u64 calculate_bytes_allowed(u64 bps_limit, unsigned long jiffy_elapsed)
{
	return mul_u64_u64_div_u64(bps_limit, (u64)jiffy_elapsed, (u64)HZ);
}

static void __tg_update_carryover(struct throtl_grp *tg, bool rw)
{
	unsigned long jiffy_elapsed = jiffies - tg->slice_start[rw];
	u64 bps_limit = tg_bps_limit(tg, rw);
	u32 iops_limit = tg_iops_limit(tg, rw);

	/*
	 * If config is updated while bios are still throttled, calculate and
	 * accumulate how many bytes/ios are waited across changes. And
	 * carryover_bytes/ios will be used to calculate new wait time under new
	 * configuration.
	 */
	if (bps_limit != U64_MAX)
		tg->carryover_bytes[rw] +=
			calculate_bytes_allowed(bps_limit, jiffy_elapsed) -
			tg->bytes_disp[rw];
	if (iops_limit != UINT_MAX)
		tg->carryover_ios[rw] +=
			calculate_io_allowed(iops_limit, jiffy_elapsed) -
			tg->io_disp[rw];
}

static void tg_update_carryover(struct throtl_grp *tg)
{
	if (tg->service_queue.nr_queued[READ])
		__tg_update_carryover(tg, READ);
	if (tg->service_queue.nr_queued[WRITE])
		__tg_update_carryover(tg, WRITE);

	/* see comments in struct throtl_grp for meaning of these fields. */
	throtl_log(&tg->service_queue, "%s: %llu %llu %u %u\n", __func__,
		   tg->carryover_bytes[READ], tg->carryover_bytes[WRITE],
		   tg->carryover_ios[READ], tg->carryover_ios[WRITE]);
}

static unsigned long tg_within_iops_limit(struct throtl_grp *tg, struct bio *bio,
				 u32 iops_limit)
{
	bool rw = bio_data_dir(bio);
	unsigned int io_allowed;
	unsigned long jiffy_elapsed, jiffy_wait, jiffy_elapsed_rnd;

	if (iops_limit == UINT_MAX) {
		return 0;
	}

	jiffy_elapsed = jiffies - tg->slice_start[rw];

	/* Round up to the next throttle slice, wait time must be nonzero */
	jiffy_elapsed_rnd = roundup(jiffy_elapsed + 1, tg->td->throtl_slice);
	io_allowed = calculate_io_allowed(iops_limit, jiffy_elapsed_rnd) +
		     tg->carryover_ios[rw];
	if (tg->io_disp[rw] + 1 <= io_allowed) {
		return 0;
	}

	/* Calc approx time to dispatch */
	jiffy_wait = jiffy_elapsed_rnd - jiffy_elapsed;
	return jiffy_wait;
}

static unsigned long tg_within_bps_limit(struct throtl_grp *tg, struct bio *bio,
				u64 bps_limit)
{
	bool rw = bio_data_dir(bio);
	u64 bytes_allowed, extra_bytes;
	unsigned long jiffy_elapsed, jiffy_wait, jiffy_elapsed_rnd;
	unsigned int bio_size = throtl_bio_data_size(bio);

	/* no need to throttle if this bio's bytes have been accounted */
	if (bps_limit == U64_MAX || bio_flagged(bio, BIO_BPS_THROTTLED)) {
		return 0;
	}

	jiffy_elapsed = jiffy_elapsed_rnd = jiffies - tg->slice_start[rw];

	/* Slice has just started. Consider one slice interval */
	if (!jiffy_elapsed)
		jiffy_elapsed_rnd = tg->td->throtl_slice;

	jiffy_elapsed_rnd = roundup(jiffy_elapsed_rnd, tg->td->throtl_slice);
	bytes_allowed = calculate_bytes_allowed(bps_limit, jiffy_elapsed_rnd) +
			tg->carryover_bytes[rw];
	if (tg->bytes_disp[rw] + bio_size <= bytes_allowed) {
		return 0;
	}

	/* Calc approx time to dispatch */
	extra_bytes = tg->bytes_disp[rw] + bio_size - bytes_allowed;
	jiffy_wait = div64_u64(extra_bytes * HZ, bps_limit);

	if (!jiffy_wait)
		jiffy_wait = 1;

	/*
	 * This wait time is without taking into consideration the rounding
	 * up we did. Add that time also.
	 */
	jiffy_wait = jiffy_wait + (jiffy_elapsed_rnd - jiffy_elapsed);
	return jiffy_wait;
}

/*
 * Returns whether one can dispatch a bio or not. Also returns approx number
 * of jiffies to wait before this bio is with-in IO rate and can be dispatched
 */
static bool tg_may_dispatch(struct throtl_grp *tg, struct bio *bio,
			    unsigned long *wait)
{
	bool rw = bio_data_dir(bio);
	unsigned long bps_wait = 0, iops_wait = 0, max_wait = 0;
	u64 bps_limit = tg_bps_limit(tg, rw);
	u32 iops_limit = tg_iops_limit(tg, rw);

	/*
 	 * Currently whole state machine of group depends on first bio
	 * queued in the group bio list. So one should not be calling
	 * this function with a different bio if there are other bios
	 * queued.
	 */
	BUG_ON(tg->service_queue.nr_queued[rw] &&
	       bio != throtl_peek_queued(&tg->service_queue.queued[rw]));

	/* If tg->bps = -1, then BW is unlimited */
	if ((bps_limit == U64_MAX && iops_limit == UINT_MAX) ||
	    tg->flags & THROTL_TG_CANCELING) {
		if (wait)
			*wait = 0;
		return true;
	}

	/*
	 * If previous slice expired, start a new one otherwise renew/extend
	 * existing slice to make sure it is at least throtl_slice interval
	 * long since now. New slice is started only for empty throttle group.
	 * If there is queued bio, that means there should be an active
	 * slice and it should be extended instead.
	 */
	if (throtl_slice_used(tg, rw) && !(tg->service_queue.nr_queued[rw]))
		throtl_start_new_slice(tg, rw, true);
	else {
		if (time_before(tg->slice_end[rw],
		    jiffies + tg->td->throtl_slice))
			throtl_extend_slice(tg, rw,
				jiffies + tg->td->throtl_slice);
	}

	bps_wait = tg_within_bps_limit(tg, bio, bps_limit);
	iops_wait = tg_within_iops_limit(tg, bio, iops_limit);
	if (bps_wait + iops_wait == 0) {
		if (wait)
			*wait = 0;
		return true;
	}

	max_wait = max(bps_wait, iops_wait);

	if (wait)
		*wait = max_wait;

	if (time_before(tg->slice_end[rw], jiffies + max_wait))
		throtl_extend_slice(tg, rw, jiffies + max_wait);

	return false;
}

static void throtl_charge_bio(struct throtl_grp *tg, struct bio *bio)
{
	bool rw = bio_data_dir(bio);
	unsigned int bio_size = throtl_bio_data_size(bio);

	/* Charge the bio to the group */
	if (!bio_flagged(bio, BIO_BPS_THROTTLED)) {
		tg->bytes_disp[rw] += bio_size;
		tg->last_bytes_disp[rw] += bio_size;
	}

	tg->io_disp[rw]++;
	tg->last_io_disp[rw]++;
}

/**
 * throtl_add_bio_tg - add a bio to the specified throtl_grp
 * @bio: bio to add
 * @qn: qnode to use
 * @tg: the target throtl_grp
 *
 * Add @bio to @tg's service_queue using @qn.  If @qn is not specified,
 * tg->qnode_on_self[] is used.
 */
static void throtl_add_bio_tg(struct bio *bio, struct throtl_qnode *qn,
			      struct throtl_grp *tg)
{
	struct throtl_service_queue *sq = &tg->service_queue;
	bool rw = bio_data_dir(bio);

	if (!qn)
		qn = &tg->qnode_on_self[rw];

	/*
	 * If @tg doesn't currently have any bios queued in the same
	 * direction, queueing @bio can change when @tg should be
	 * dispatched.  Mark that @tg was empty.  This is automatically
	 * cleared on the next tg_update_disptime().
	 */
	if (!sq->nr_queued[rw])
		tg->flags |= THROTL_TG_WAS_EMPTY;

	throtl_qnode_add_bio(bio, qn, &sq->queued[rw]);

	sq->nr_queued[rw]++;
	throtl_enqueue_tg(tg);
}

static void tg_update_disptime(struct throtl_grp *tg)
{
	struct throtl_service_queue *sq = &tg->service_queue;
	unsigned long read_wait = -1, write_wait = -1, min_wait = -1, disptime;
	struct bio *bio;

	bio = throtl_peek_queued(&sq->queued[READ]);
	if (bio)
		tg_may_dispatch(tg, bio, &read_wait);

	bio = throtl_peek_queued(&sq->queued[WRITE]);
	if (bio)
		tg_may_dispatch(tg, bio, &write_wait);

	min_wait = min(read_wait, write_wait);
	disptime = jiffies + min_wait;

	/* Update dispatch time */
	throtl_rb_erase(&tg->rb_node, tg->service_queue.parent_sq);
	tg->disptime = disptime;
	tg_service_queue_add(tg);

	/* see throtl_add_bio_tg() */
	tg->flags &= ~THROTL_TG_WAS_EMPTY;
}

static void start_parent_slice_with_credit(struct throtl_grp *child_tg,
					struct throtl_grp *parent_tg, bool rw)
{
	if (throtl_slice_used(parent_tg, rw)) {
		throtl_start_new_slice_with_credit(parent_tg, rw,
				child_tg->slice_start[rw]);
	}

}

static void tg_dispatch_one_bio(struct throtl_grp *tg, bool rw)
{
	struct throtl_service_queue *sq = &tg->service_queue;
	struct throtl_service_queue *parent_sq = sq->parent_sq;
	struct throtl_grp *parent_tg = sq_to_tg(parent_sq);
	struct throtl_grp *tg_to_put = NULL;
	struct bio *bio;

	/*
	 * @bio is being transferred from @tg to @parent_sq.  Popping a bio
	 * from @tg may put its reference and @parent_sq might end up
	 * getting released prematurely.  Remember the tg to put and put it
	 * after @bio is transferred to @parent_sq.
	 */
	bio = throtl_pop_queued(&sq->queued[rw], &tg_to_put);
	sq->nr_queued[rw]--;

	throtl_charge_bio(tg, bio);

	/*
	 * If our parent is another tg, we just need to transfer @bio to
	 * the parent using throtl_add_bio_tg().  If our parent is
	 * @td->service_queue, @bio is ready to be issued.  Put it on its
	 * bio_lists[] and decrease total number queued.  The caller is
	 * responsible for issuing these bios.
	 */
	if (parent_tg) {
		throtl_add_bio_tg(bio, &tg->qnode_on_parent[rw], parent_tg);
		start_parent_slice_with_credit(tg, parent_tg, rw);
	} else {
		bio_set_flag(bio, BIO_BPS_THROTTLED);
		throtl_qnode_add_bio(bio, &tg->qnode_on_parent[rw],
				     &parent_sq->queued[rw]);
		BUG_ON(tg->td->nr_queued[rw] <= 0);
		tg->td->nr_queued[rw]--;
	}

	throtl_trim_slice(tg, rw);

	if (tg_to_put)
		blkg_put(tg_to_blkg(tg_to_put));
}

static int throtl_dispatch_tg(struct throtl_grp *tg)
{
	struct throtl_service_queue *sq = &tg->service_queue;
	unsigned int nr_reads = 0, nr_writes = 0;
	unsigned int max_nr_reads = THROTL_GRP_QUANTUM * 3 / 4;
	unsigned int max_nr_writes = THROTL_GRP_QUANTUM - max_nr_reads;
	struct bio *bio;

	/* Try to dispatch 75% READS and 25% WRITES */

	while ((bio = throtl_peek_queued(&sq->queued[READ])) &&
	       tg_may_dispatch(tg, bio, NULL)) {

		tg_dispatch_one_bio(tg, bio_data_dir(bio));
		nr_reads++;

		if (nr_reads >= max_nr_reads)
			break;
	}

	while ((bio = throtl_peek_queued(&sq->queued[WRITE])) &&
	       tg_may_dispatch(tg, bio, NULL)) {

		tg_dispatch_one_bio(tg, bio_data_dir(bio));
		nr_writes++;

		if (nr_writes >= max_nr_writes)
			break;
	}

	return nr_reads + nr_writes;
}

static int throtl_select_dispatch(struct throtl_service_queue *parent_sq)
{
	unsigned int nr_disp = 0;

	while (1) {
		struct throtl_grp *tg;
		struct throtl_service_queue *sq;

		if (!parent_sq->nr_pending)
			break;

		tg = throtl_rb_first(parent_sq);
		if (!tg)
			break;

		if (time_before(jiffies, tg->disptime))
			break;

		nr_disp += throtl_dispatch_tg(tg);

		sq = &tg->service_queue;
		if (sq->nr_queued[READ] || sq->nr_queued[WRITE])
			tg_update_disptime(tg);
		else
			throtl_dequeue_tg(tg);

		if (nr_disp >= THROTL_QUANTUM)
			break;
	}

	return nr_disp;
}

static bool throtl_can_upgrade(struct throtl_data *td,
	struct throtl_grp *this_tg);
/**
 * throtl_pending_timer_fn - timer function for service_queue->pending_timer
 * @t: the pending_timer member of the throtl_service_queue being serviced
 *
 * This timer is armed when a child throtl_grp with active bio's become
 * pending and queued on the service_queue's pending_tree and expires when
 * the first child throtl_grp should be dispatched.  This function
 * dispatches bio's from the children throtl_grps to the parent
 * service_queue.
 *
 * If the parent's parent is another throtl_grp, dispatching is propagated
 * by either arming its pending_timer or repeating dispatch directly.  If
 * the top-level service_tree is reached, throtl_data->dispatch_work is
 * kicked so that the ready bio's are issued.
 */
static void throtl_pending_timer_fn(struct timer_list *t)
{
	struct throtl_service_queue *sq = from_timer(sq, t, pending_timer);
	struct throtl_grp *tg = sq_to_tg(sq);
	struct throtl_data *td = sq_to_td(sq);
	struct throtl_service_queue *parent_sq;
	struct request_queue *q;
	bool dispatched;
	int ret;

	/* throtl_data may be gone, so figure out request queue by blkg */
	if (tg)
		q = tg->pd.blkg->q;
	else
		q = td->queue;

	spin_lock_irq(&q->queue_lock);

	if (!q->root_blkg)
		goto out_unlock;

	if (throtl_can_upgrade(td, NULL))
		throtl_upgrade_state(td);

again:
	parent_sq = sq->parent_sq;
	dispatched = false;

	while (true) {
		throtl_log(sq, "dispatch nr_queued=%u read=%u write=%u",
			   sq->nr_queued[READ] + sq->nr_queued[WRITE],
			   sq->nr_queued[READ], sq->nr_queued[WRITE]);

		ret = throtl_select_dispatch(sq);
		if (ret) {
			throtl_log(sq, "bios disp=%u", ret);
			dispatched = true;
		}

		if (throtl_schedule_next_dispatch(sq, false))
			break;

		/* this dispatch windows is still open, relax and repeat */
		spin_unlock_irq(&q->queue_lock);
		cpu_relax();
		spin_lock_irq(&q->queue_lock);
	}

	if (!dispatched)
		goto out_unlock;

	if (parent_sq) {
		/* @parent_sq is another throl_grp, propagate dispatch */
		if (tg->flags & THROTL_TG_WAS_EMPTY) {
			tg_update_disptime(tg);
			if (!throtl_schedule_next_dispatch(parent_sq, false)) {
				/* window is already open, repeat dispatching */
				sq = parent_sq;
				tg = sq_to_tg(sq);
				goto again;
			}
		}
	} else {
		/* reached the top-level, queue issuing */
		queue_work(kthrotld_workqueue, &td->dispatch_work);
	}
out_unlock:
	spin_unlock_irq(&q->queue_lock);
}

/**
 * blk_throtl_dispatch_work_fn - work function for throtl_data->dispatch_work
 * @work: work item being executed
 *
 * This function is queued for execution when bios reach the bio_lists[]
 * of throtl_data->service_queue.  Those bios are ready and issued by this
 * function.
 */
static void blk_throtl_dispatch_work_fn(struct work_struct *work)
{
	struct throtl_data *td = container_of(work, struct throtl_data,
					      dispatch_work);
	struct throtl_service_queue *td_sq = &td->service_queue;
	struct request_queue *q = td->queue;
	struct bio_list bio_list_on_stack;
	struct bio *bio;
	struct blk_plug plug;
	int rw;

	bio_list_init(&bio_list_on_stack);

	spin_lock_irq(&q->queue_lock);
	for (rw = READ; rw <= WRITE; rw++)
		while ((bio = throtl_pop_queued(&td_sq->queued[rw], NULL)))
			bio_list_add(&bio_list_on_stack, bio);
	spin_unlock_irq(&q->queue_lock);

	if (!bio_list_empty(&bio_list_on_stack)) {
		blk_start_plug(&plug);
		while ((bio = bio_list_pop(&bio_list_on_stack)))
			submit_bio_noacct_nocheck(bio);
		blk_finish_plug(&plug);
	}
}

static u64 tg_prfill_conf_u64(struct seq_file *sf, struct blkg_policy_data *pd,
			      int off)
{
	struct throtl_grp *tg = pd_to_tg(pd);
	u64 v = *(u64 *)((void *)tg + off);

	if (v == U64_MAX)
		return 0;
	return __blkg_prfill_u64(sf, pd, v);
}

static u64 tg_prfill_conf_uint(struct seq_file *sf, struct blkg_policy_data *pd,
			       int off)
{
	struct throtl_grp *tg = pd_to_tg(pd);
	unsigned int v = *(unsigned int *)((void *)tg + off);

	if (v == UINT_MAX)
		return 0;
	return __blkg_prfill_u64(sf, pd, v);
}

static int tg_print_conf_u64(struct seq_file *sf, void *v)
{
	blkcg_print_blkgs(sf, css_to_blkcg(seq_css(sf)), tg_prfill_conf_u64,
			  &blkcg_policy_throtl, seq_cft(sf)->private, false);
	return 0;
}

static int tg_print_conf_uint(struct seq_file *sf, void *v)
{
	blkcg_print_blkgs(sf, css_to_blkcg(seq_css(sf)), tg_prfill_conf_uint,
			  &blkcg_policy_throtl, seq_cft(sf)->private, false);
	return 0;
}

static void tg_conf_updated(struct throtl_grp *tg, bool global)
{
	struct throtl_service_queue *sq = &tg->service_queue;
	struct cgroup_subsys_state *pos_css;
	struct blkcg_gq *blkg;

	throtl_log(&tg->service_queue,
		   "limit change rbps=%llu wbps=%llu riops=%u wiops=%u",
		   tg_bps_limit(tg, READ), tg_bps_limit(tg, WRITE),
		   tg_iops_limit(tg, READ), tg_iops_limit(tg, WRITE));

	/*
	 * Update has_rules[] flags for the updated tg's subtree.  A tg is
	 * considered to have rules if either the tg itself or any of its
	 * ancestors has rules.  This identifies groups without any
	 * restrictions in the whole hierarchy and allows them to bypass
	 * blk-throttle.
	 */
	blkg_for_each_descendant_pre(blkg, pos_css,
			global ? tg->td->queue->root_blkg : tg_to_blkg(tg)) {
		struct throtl_grp *this_tg = blkg_to_tg(blkg);
		struct throtl_grp *parent_tg;

		tg_update_has_rules(this_tg);
		/* ignore root/second level */
		if (!cgroup_subsys_on_dfl(io_cgrp_subsys) || !blkg->parent ||
		    !blkg->parent->parent)
			continue;
		parent_tg = blkg_to_tg(blkg->parent);
		/*
		 * make sure all children has lower idle time threshold and
		 * higher latency target
		 */
		this_tg->idletime_threshold = min(this_tg->idletime_threshold,
				parent_tg->idletime_threshold);
		this_tg->latency_target = max(this_tg->latency_target,
				parent_tg->latency_target);
	}

	/*
	 * We're already holding queue_lock and know @tg is valid.  Let's
	 * apply the new config directly.
	 *
	 * Restart the slices for both READ and WRITES. It might happen
	 * that a group's limit are dropped suddenly and we don't want to
	 * account recently dispatched IO with new low rate.
	 */
	throtl_start_new_slice(tg, READ, false);
	throtl_start_new_slice(tg, WRITE, false);

	if (tg->flags & THROTL_TG_PENDING) {
		tg_update_disptime(tg);
		throtl_schedule_next_dispatch(sq->parent_sq, true);
	}
}

static ssize_t tg_set_conf(struct kernfs_open_file *of,
			   char *buf, size_t nbytes, loff_t off, bool is_u64)
{
	struct blkcg *blkcg = css_to_blkcg(of_css(of));
	struct blkg_conf_ctx ctx;
	struct throtl_grp *tg;
	int ret;
	u64 v;

	blkg_conf_init(&ctx, buf);

	ret = blkg_conf_prep(blkcg, &blkcg_policy_throtl, &ctx);
	if (ret)
		goto out_finish;

	ret = -EINVAL;
	if (sscanf(ctx.body, "%llu", &v) != 1)
		goto out_finish;
	if (!v)
		v = U64_MAX;

	tg = blkg_to_tg(ctx.blkg);
	tg_update_carryover(tg);

	if (is_u64)
		*(u64 *)((void *)tg + of_cft(of)->private) = v;
	else
		*(unsigned int *)((void *)tg + of_cft(of)->private) = v;

	tg_conf_updated(tg, false);
	ret = 0;
out_finish:
	blkg_conf_exit(&ctx);
	return ret ?: nbytes;
}

static ssize_t tg_set_conf_u64(struct kernfs_open_file *of,
			       char *buf, size_t nbytes, loff_t off)
{
	return tg_set_conf(of, buf, nbytes, off, true);
}

static ssize_t tg_set_conf_uint(struct kernfs_open_file *of,
				char *buf, size_t nbytes, loff_t off)
{
	return tg_set_conf(of, buf, nbytes, off, false);
}

static int tg_print_rwstat(struct seq_file *sf, void *v)
{
	blkcg_print_blkgs(sf, css_to_blkcg(seq_css(sf)),
			  blkg_prfill_rwstat, &blkcg_policy_throtl,
			  seq_cft(sf)->private, true);
	return 0;
}

static u64 tg_prfill_rwstat_recursive(struct seq_file *sf,
				      struct blkg_policy_data *pd, int off)
{
	struct blkg_rwstat_sample sum;

	blkg_rwstat_recursive_sum(pd_to_blkg(pd), &blkcg_policy_throtl, off,
				  &sum);
	return __blkg_prfill_rwstat(sf, pd, &sum);
}

static int tg_print_rwstat_recursive(struct seq_file *sf, void *v)
{
	blkcg_print_blkgs(sf, css_to_blkcg(seq_css(sf)),
			  tg_prfill_rwstat_recursive, &blkcg_policy_throtl,
			  seq_cft(sf)->private, true);
	return 0;
}

static struct cftype throtl_legacy_files[] = {
	{
		.name = "throttle.read_bps_device",
		.private = offsetof(struct throtl_grp, bps[READ][LIMIT_MAX]),
		.seq_show = tg_print_conf_u64,
		.write = tg_set_conf_u64,
	},
	{
		.name = "throttle.write_bps_device",
		.private = offsetof(struct throtl_grp, bps[WRITE][LIMIT_MAX]),
		.seq_show = tg_print_conf_u64,
		.write = tg_set_conf_u64,
	},
	{
		.name = "throttle.read_iops_device",
		.private = offsetof(struct throtl_grp, iops[READ][LIMIT_MAX]),
		.seq_show = tg_print_conf_uint,
		.write = tg_set_conf_uint,
	},
	{
		.name = "throttle.write_iops_device",
		.private = offsetof(struct throtl_grp, iops[WRITE][LIMIT_MAX]),
		.seq_show = tg_print_conf_uint,
		.write = tg_set_conf_uint,
	},
	{
		.name = "throttle.io_service_bytes",
		.private = offsetof(struct throtl_grp, stat_bytes),
		.seq_show = tg_print_rwstat,
	},
	{
		.name = "throttle.io_service_bytes_recursive",
		.private = offsetof(struct throtl_grp, stat_bytes),
		.seq_show = tg_print_rwstat_recursive,
	},
	{
		.name = "throttle.io_serviced",
		.private = offsetof(struct throtl_grp, stat_ios),
		.seq_show = tg_print_rwstat,
	},
	{
		.name = "throttle.io_serviced_recursive",
		.private = offsetof(struct throtl_grp, stat_ios),
		.seq_show = tg_print_rwstat_recursive,
	},
	{ }	/* terminate */
};

static u64 tg_prfill_limit(struct seq_file *sf, struct blkg_policy_data *pd,
			 int off)
{
	struct throtl_grp *tg = pd_to_tg(pd);
	const char *dname = blkg_dev_name(pd->blkg);
	char bufs[4][21] = { "max", "max", "max", "max" };
	u64 bps_dft;
	unsigned int iops_dft;
	char idle_time[26] = "";
	char latency_time[26] = "";

	if (!dname)
		return 0;

	if (off == LIMIT_LOW) {
		bps_dft = 0;
		iops_dft = 0;
	} else {
		bps_dft = U64_MAX;
		iops_dft = UINT_MAX;
	}

	if (tg->bps_conf[READ][off] == bps_dft &&
	    tg->bps_conf[WRITE][off] == bps_dft &&
	    tg->iops_conf[READ][off] == iops_dft &&
	    tg->iops_conf[WRITE][off] == iops_dft &&
	    (off != LIMIT_LOW ||
	     (tg->idletime_threshold_conf == DFL_IDLE_THRESHOLD &&
	      tg->latency_target_conf == DFL_LATENCY_TARGET)))
		return 0;

	if (tg->bps_conf[READ][off] != U64_MAX)
		snprintf(bufs[0], sizeof(bufs[0]), "%llu",
			tg->bps_conf[READ][off]);
	if (tg->bps_conf[WRITE][off] != U64_MAX)
		snprintf(bufs[1], sizeof(bufs[1]), "%llu",
			tg->bps_conf[WRITE][off]);
	if (tg->iops_conf[READ][off] != UINT_MAX)
		snprintf(bufs[2], sizeof(bufs[2]), "%u",
			tg->iops_conf[READ][off]);
	if (tg->iops_conf[WRITE][off] != UINT_MAX)
		snprintf(bufs[3], sizeof(bufs[3]), "%u",
			tg->iops_conf[WRITE][off]);
	if (off == LIMIT_LOW) {
		if (tg->idletime_threshold_conf == ULONG_MAX)
			strcpy(idle_time, " idle=max");
		else
			snprintf(idle_time, sizeof(idle_time), " idle=%lu",
				tg->idletime_threshold_conf);

		if (tg->latency_target_conf == ULONG_MAX)
			strcpy(latency_time, " latency=max");
		else
			snprintf(latency_time, sizeof(latency_time),
				" latency=%lu", tg->latency_target_conf);
	}

	seq_printf(sf, "%s rbps=%s wbps=%s riops=%s wiops=%s%s%s\n",
		   dname, bufs[0], bufs[1], bufs[2], bufs[3], idle_time,
		   latency_time);
	return 0;
}

static int tg_print_limit(struct seq_file *sf, void *v)
{
	blkcg_print_blkgs(sf, css_to_blkcg(seq_css(sf)), tg_prfill_limit,
			  &blkcg_policy_throtl, seq_cft(sf)->private, false);
	return 0;
}

static ssize_t tg_set_limit(struct kernfs_open_file *of,
			  char *buf, size_t nbytes, loff_t off)
{
	struct blkcg *blkcg = css_to_blkcg(of_css(of));
	struct blkg_conf_ctx ctx;
	struct throtl_grp *tg;
	u64 v[4];
	unsigned long idle_time;
	unsigned long latency_time;
	int ret;
	int index = of_cft(of)->private;

	blkg_conf_init(&ctx, buf);

	ret = blkg_conf_prep(blkcg, &blkcg_policy_throtl, &ctx);
	if (ret)
		goto out_finish;

	tg = blkg_to_tg(ctx.blkg);
	tg_update_carryover(tg);

	v[0] = tg->bps_conf[READ][index];
	v[1] = tg->bps_conf[WRITE][index];
	v[2] = tg->iops_conf[READ][index];
	v[3] = tg->iops_conf[WRITE][index];

	idle_time = tg->idletime_threshold_conf;
	latency_time = tg->latency_target_conf;
	while (true) {
		char tok[27];	/* wiops=18446744073709551616 */
		char *p;
		u64 val = U64_MAX;
		int len;

		if (sscanf(ctx.body, "%26s%n", tok, &len) != 1)
			break;
		if (tok[0] == '\0')
			break;
		ctx.body += len;

		ret = -EINVAL;
		p = tok;
		strsep(&p, "=");
		if (!p || (sscanf(p, "%llu", &val) != 1 && strcmp(p, "max")))
			goto out_finish;

		ret = -ERANGE;
		if (!val)
			goto out_finish;

		ret = -EINVAL;
		if (!strcmp(tok, "rbps") && val > 1)
			v[0] = val;
		else if (!strcmp(tok, "wbps") && val > 1)
			v[1] = val;
		else if (!strcmp(tok, "riops") && val > 1)
			v[2] = min_t(u64, val, UINT_MAX);
		else if (!strcmp(tok, "wiops") && val > 1)
			v[3] = min_t(u64, val, UINT_MAX);
		else if (off == LIMIT_LOW && !strcmp(tok, "idle"))
			idle_time = val;
		else if (off == LIMIT_LOW && !strcmp(tok, "latency"))
			latency_time = val;
		else
			goto out_finish;
	}

	tg->bps_conf[READ][index] = v[0];
	tg->bps_conf[WRITE][index] = v[1];
	tg->iops_conf[READ][index] = v[2];
	tg->iops_conf[WRITE][index] = v[3];

	if (index == LIMIT_MAX) {
		tg->bps[READ][index] = v[0];
		tg->bps[WRITE][index] = v[1];
		tg->iops[READ][index] = v[2];
		tg->iops[WRITE][index] = v[3];
	}
	tg->bps[READ][LIMIT_LOW] = min(tg->bps_conf[READ][LIMIT_LOW],
		tg->bps_conf[READ][LIMIT_MAX]);
	tg->bps[WRITE][LIMIT_LOW] = min(tg->bps_conf[WRITE][LIMIT_LOW],
		tg->bps_conf[WRITE][LIMIT_MAX]);
	tg->iops[READ][LIMIT_LOW] = min(tg->iops_conf[READ][LIMIT_LOW],
		tg->iops_conf[READ][LIMIT_MAX]);
	tg->iops[WRITE][LIMIT_LOW] = min(tg->iops_conf[WRITE][LIMIT_LOW],
		tg->iops_conf[WRITE][LIMIT_MAX]);
	tg->idletime_threshold_conf = idle_time;
	tg->latency_target_conf = latency_time;

	/* force user to configure all settings for low limit  */
	if (!(tg->bps[READ][LIMIT_LOW] || tg->iops[READ][LIMIT_LOW] ||
	      tg->bps[WRITE][LIMIT_LOW] || tg->iops[WRITE][LIMIT_LOW]) ||
	    tg->idletime_threshold_conf == DFL_IDLE_THRESHOLD ||
	    tg->latency_target_conf == DFL_LATENCY_TARGET) {
		tg->bps[READ][LIMIT_LOW] = 0;
		tg->bps[WRITE][LIMIT_LOW] = 0;
		tg->iops[READ][LIMIT_LOW] = 0;
		tg->iops[WRITE][LIMIT_LOW] = 0;
		tg->idletime_threshold = DFL_IDLE_THRESHOLD;
		tg->latency_target = DFL_LATENCY_TARGET;
	} else if (index == LIMIT_LOW) {
		tg->idletime_threshold = tg->idletime_threshold_conf;
		tg->latency_target = tg->latency_target_conf;
	}

	blk_throtl_update_limit_valid(tg->td);
	if (tg->td->limit_valid[LIMIT_LOW]) {
		if (index == LIMIT_LOW)
			tg->td->limit_index = LIMIT_LOW;
	} else
		tg->td->limit_index = LIMIT_MAX;
	tg_conf_updated(tg, index == LIMIT_LOW &&
		tg->td->limit_valid[LIMIT_LOW]);
	ret = 0;
out_finish:
	blkg_conf_exit(&ctx);
	return ret ?: nbytes;
}

static struct cftype throtl_files[] = {
#ifdef CONFIG_BLK_DEV_THROTTLING_LOW
	{
		.name = "low",
		.flags = CFTYPE_NOT_ON_ROOT,
		.seq_show = tg_print_limit,
		.write = tg_set_limit,
		.private = LIMIT_LOW,
	},
#endif
	{
		.name = "max",
		.flags = CFTYPE_NOT_ON_ROOT,
		.seq_show = tg_print_limit,
		.write = tg_set_limit,
		.private = LIMIT_MAX,
	},
	{ }	/* terminate */
};

static void throtl_shutdown_wq(struct request_queue *q)
{
	struct throtl_data *td = q->td;

	cancel_work_sync(&td->dispatch_work);
}

struct blkcg_policy blkcg_policy_throtl = {
	.dfl_cftypes		= throtl_files,
	.legacy_cftypes		= throtl_legacy_files,

	.pd_alloc_fn		= throtl_pd_alloc,
	.pd_init_fn		= throtl_pd_init,
	.pd_online_fn		= throtl_pd_online,
	.pd_offline_fn		= throtl_pd_offline,
	.pd_free_fn		= throtl_pd_free,
};

void blk_throtl_cancel_bios(struct gendisk *disk)
{
	struct request_queue *q = disk->queue;
	struct cgroup_subsys_state *pos_css;
	struct blkcg_gq *blkg;

	spin_lock_irq(&q->queue_lock);
	/*
	 * queue_lock is held, rcu lock is not needed here technically.
	 * However, rcu lock is still held to emphasize that following
	 * path need RCU protection and to prevent warning from lockdep.
	 */
	rcu_read_lock();
	blkg_for_each_descendant_post(blkg, pos_css, q->root_blkg) {
		struct throtl_grp *tg = blkg_to_tg(blkg);
		struct throtl_service_queue *sq = &tg->service_queue;

		/*
		 * Set the flag to make sure throtl_pending_timer_fn() won't
		 * stop until all throttled bios are dispatched.
		 */
		tg->flags |= THROTL_TG_CANCELING;

		/*
		 * Do not dispatch cgroup without THROTL_TG_PENDING or cgroup
		 * will be inserted to service queue without THROTL_TG_PENDING
		 * set in tg_update_disptime below. Then IO dispatched from
		 * child in tg_dispatch_one_bio will trigger double insertion
		 * and corrupt the tree.
		 */
		if (!(tg->flags & THROTL_TG_PENDING))
			continue;

		/*
		 * Update disptime after setting the above flag to make sure
		 * throtl_select_dispatch() won't exit without dispatching.
		 */
		tg_update_disptime(tg);

		throtl_schedule_pending_timer(sq, jiffies + 1);
	}
	rcu_read_unlock();
	spin_unlock_irq(&q->queue_lock);
}

#ifdef CONFIG_BLK_DEV_THROTTLING_LOW
static unsigned long __tg_last_low_overflow_time(struct throtl_grp *tg)
{
	unsigned long rtime = jiffies, wtime = jiffies;

	if (tg->bps[READ][LIMIT_LOW] || tg->iops[READ][LIMIT_LOW])
		rtime = tg->last_low_overflow_time[READ];
	if (tg->bps[WRITE][LIMIT_LOW] || tg->iops[WRITE][LIMIT_LOW])
		wtime = tg->last_low_overflow_time[WRITE];
	return min(rtime, wtime);
}

static unsigned long tg_last_low_overflow_time(struct throtl_grp *tg)
{
	struct throtl_service_queue *parent_sq;
	struct throtl_grp *parent = tg;
	unsigned long ret = __tg_last_low_overflow_time(tg);

	while (true) {
		parent_sq = parent->service_queue.parent_sq;
		parent = sq_to_tg(parent_sq);
		if (!parent)
			break;

		/*
		 * The parent doesn't have low limit, it always reaches low
		 * limit. Its overflow time is useless for children
		 */
		if (!parent->bps[READ][LIMIT_LOW] &&
		    !parent->iops[READ][LIMIT_LOW] &&
		    !parent->bps[WRITE][LIMIT_LOW] &&
		    !parent->iops[WRITE][LIMIT_LOW])
			continue;
		if (time_after(__tg_last_low_overflow_time(parent), ret))
			ret = __tg_last_low_overflow_time(parent);
	}
	return ret;
}

static bool throtl_tg_is_idle(struct throtl_grp *tg)
{
	/*
	 * cgroup is idle if:
	 * - single idle is too long, longer than a fixed value (in case user
	 *   configure a too big threshold) or 4 times of idletime threshold
	 * - average think time is more than threshold
	 * - IO latency is largely below threshold
	 */
	unsigned long time;
	bool ret;

	time = min_t(unsigned long, MAX_IDLE_TIME, 4 * tg->idletime_threshold);
	ret = tg->latency_target == DFL_LATENCY_TARGET ||
	      tg->idletime_threshold == DFL_IDLE_THRESHOLD ||
	      (ktime_get_ns() >> 10) - tg->last_finish_time > time ||
	      tg->avg_idletime > tg->idletime_threshold ||
	      (tg->latency_target && tg->bio_cnt &&
		tg->bad_bio_cnt * 5 < tg->bio_cnt);
	throtl_log(&tg->service_queue,
		"avg_idle=%ld, idle_threshold=%ld, bad_bio=%d, total_bio=%d, is_idle=%d, scale=%d",
		tg->avg_idletime, tg->idletime_threshold, tg->bad_bio_cnt,
		tg->bio_cnt, ret, tg->td->scale);
	return ret;
}

static bool throtl_low_limit_reached(struct throtl_grp *tg, int rw)
{
	struct throtl_service_queue *sq = &tg->service_queue;
	bool limit = tg->bps[rw][LIMIT_LOW] || tg->iops[rw][LIMIT_LOW];

	/*
	 * if low limit is zero, low limit is always reached.
	 * if low limit is non-zero, we can check if there is any request
	 * is queued to determine if low limit is reached as we throttle
	 * request according to limit.
	 */
	return !limit || sq->nr_queued[rw];
}

static bool throtl_tg_can_upgrade(struct throtl_grp *tg)
{
	/*
	 * cgroup reaches low limit when low limit of READ and WRITE are
	 * both reached, it's ok to upgrade to next limit if cgroup reaches
	 * low limit
	 */
	if (throtl_low_limit_reached(tg, READ) &&
	    throtl_low_limit_reached(tg, WRITE))
		return true;

	if (time_after_eq(jiffies,
		tg_last_low_overflow_time(tg) + tg->td->throtl_slice) &&
	    throtl_tg_is_idle(tg))
		return true;
	return false;
}

static bool throtl_hierarchy_can_upgrade(struct throtl_grp *tg)
{
	while (true) {
		if (throtl_tg_can_upgrade(tg))
			return true;
		tg = sq_to_tg(tg->service_queue.parent_sq);
		if (!tg || !tg_to_blkg(tg)->parent)
			return false;
	}
	return false;
}

static bool throtl_can_upgrade(struct throtl_data *td,
	struct throtl_grp *this_tg)
{
	struct cgroup_subsys_state *pos_css;
	struct blkcg_gq *blkg;

	if (td->limit_index != LIMIT_LOW)
		return false;

	if (time_before(jiffies, td->low_downgrade_time + td->throtl_slice))
		return false;

	rcu_read_lock();
	blkg_for_each_descendant_post(blkg, pos_css, td->queue->root_blkg) {
		struct throtl_grp *tg = blkg_to_tg(blkg);

		if (tg == this_tg)
			continue;
		if (!list_empty(&tg_to_blkg(tg)->blkcg->css.children))
			continue;
		if (!throtl_hierarchy_can_upgrade(tg)) {
			rcu_read_unlock();
			return false;
		}
	}
	rcu_read_unlock();
	return true;
}

static void throtl_upgrade_check(struct throtl_grp *tg)
{
	unsigned long now = jiffies;

	if (tg->td->limit_index != LIMIT_LOW)
		return;

	if (time_after(tg->last_check_time + tg->td->throtl_slice, now))
		return;

	tg->last_check_time = now;

	if (!time_after_eq(now,
	     __tg_last_low_overflow_time(tg) + tg->td->throtl_slice))
		return;

	if (throtl_can_upgrade(tg->td, NULL))
		throtl_upgrade_state(tg->td);
}

static void throtl_upgrade_state(struct throtl_data *td)
{
	struct cgroup_subsys_state *pos_css;
	struct blkcg_gq *blkg;

	throtl_log(&td->service_queue, "upgrade to max");
	td->limit_index = LIMIT_MAX;
	td->low_upgrade_time = jiffies;
	td->scale = 0;
	rcu_read_lock();
	blkg_for_each_descendant_post(blkg, pos_css, td->queue->root_blkg) {
		struct throtl_grp *tg = blkg_to_tg(blkg);
		struct throtl_service_queue *sq = &tg->service_queue;

		tg->disptime = jiffies - 1;
		throtl_select_dispatch(sq);
		throtl_schedule_next_dispatch(sq, true);
	}
	rcu_read_unlock();
	throtl_select_dispatch(&td->service_queue);
	throtl_schedule_next_dispatch(&td->service_queue, true);
	queue_work(kthrotld_workqueue, &td->dispatch_work);
}

static void throtl_downgrade_state(struct throtl_data *td)
{
	td->scale /= 2;

	throtl_log(&td->service_queue, "downgrade, scale %d", td->scale);
	if (td->scale) {
		td->low_upgrade_time = jiffies - td->scale * td->throtl_slice;
		return;
	}

	td->limit_index = LIMIT_LOW;
	td->low_downgrade_time = jiffies;
}

static bool throtl_tg_can_downgrade(struct throtl_grp *tg)
{
	struct throtl_data *td = tg->td;
	unsigned long now = jiffies;

	/*
	 * If cgroup is below low limit, consider downgrade and throttle other
	 * cgroups
	 */
	if (time_after_eq(now, tg_last_low_overflow_time(tg) +
					td->throtl_slice) &&
	    (!throtl_tg_is_idle(tg) ||
	     !list_empty(&tg_to_blkg(tg)->blkcg->css.children)))
		return true;
	return false;
}

static bool throtl_hierarchy_can_downgrade(struct throtl_grp *tg)
{
	struct throtl_data *td = tg->td;

	if (time_before(jiffies, td->low_upgrade_time + td->throtl_slice))
		return false;

	while (true) {
		if (!throtl_tg_can_downgrade(tg))
			return false;
		tg = sq_to_tg(tg->service_queue.parent_sq);
		if (!tg || !tg_to_blkg(tg)->parent)
			break;
	}
	return true;
}

static void throtl_downgrade_check(struct throtl_grp *tg)
{
	uint64_t bps;
	unsigned int iops;
	unsigned long elapsed_time;
	unsigned long now = jiffies;

	if (tg->td->limit_index != LIMIT_MAX ||
	    !tg->td->limit_valid[LIMIT_LOW])
		return;
	if (!list_empty(&tg_to_blkg(tg)->blkcg->css.children))
		return;
	if (time_after(tg->last_check_time + tg->td->throtl_slice, now))
		return;

	elapsed_time = now - tg->last_check_time;
	tg->last_check_time = now;

	if (time_before(now, tg_last_low_overflow_time(tg) +
			tg->td->throtl_slice))
		return;

	if (tg->bps[READ][LIMIT_LOW]) {
		bps = tg->last_bytes_disp[READ] * HZ;
		do_div(bps, elapsed_time);
		if (bps >= tg->bps[READ][LIMIT_LOW])
			tg->last_low_overflow_time[READ] = now;
	}

	if (tg->bps[WRITE][LIMIT_LOW]) {
		bps = tg->last_bytes_disp[WRITE] * HZ;
		do_div(bps, elapsed_time);
		if (bps >= tg->bps[WRITE][LIMIT_LOW])
			tg->last_low_overflow_time[WRITE] = now;
	}

	if (tg->iops[READ][LIMIT_LOW]) {
		iops = tg->last_io_disp[READ] * HZ / elapsed_time;
		if (iops >= tg->iops[READ][LIMIT_LOW])
			tg->last_low_overflow_time[READ] = now;
	}

	if (tg->iops[WRITE][LIMIT_LOW]) {
		iops = tg->last_io_disp[WRITE] * HZ / elapsed_time;
		if (iops >= tg->iops[WRITE][LIMIT_LOW])
			tg->last_low_overflow_time[WRITE] = now;
	}

	/*
	 * If cgroup is below low limit, consider downgrade and throttle other
	 * cgroups
	 */
	if (throtl_hierarchy_can_downgrade(tg))
		throtl_downgrade_state(tg->td);

	tg->last_bytes_disp[READ] = 0;
	tg->last_bytes_disp[WRITE] = 0;
	tg->last_io_disp[READ] = 0;
	tg->last_io_disp[WRITE] = 0;
}

static void blk_throtl_update_idletime(struct throtl_grp *tg)
{
	unsigned long now;
	unsigned long last_finish_time = tg->last_finish_time;

	if (last_finish_time == 0)
		return;

	now = ktime_get_ns() >> 10;
	if (now <= last_finish_time ||
	    last_finish_time == tg->checked_last_finish_time)
		return;

	tg->avg_idletime = (tg->avg_idletime * 7 + now - last_finish_time) >> 3;
	tg->checked_last_finish_time = last_finish_time;
}

static void throtl_update_latency_buckets(struct throtl_data *td)
{
	struct avg_latency_bucket avg_latency[2][LATENCY_BUCKET_SIZE];
	int i, cpu, rw;
	unsigned long last_latency[2] = { 0 };
	unsigned long latency[2];

	if (!blk_queue_nonrot(td->queue) || !td->limit_valid[LIMIT_LOW])
		return;
	if (time_before(jiffies, td->last_calculate_time + HZ))
		return;
	td->last_calculate_time = jiffies;

	memset(avg_latency, 0, sizeof(avg_latency));
	for (rw = READ; rw <= WRITE; rw++) {
		for (i = 0; i < LATENCY_BUCKET_SIZE; i++) {
			struct latency_bucket *tmp = &td->tmp_buckets[rw][i];

			for_each_possible_cpu(cpu) {
				struct latency_bucket *bucket;

				/* this isn't race free, but ok in practice */
				bucket = per_cpu_ptr(td->latency_buckets[rw],
					cpu);
				tmp->total_latency += bucket[i].total_latency;
				tmp->samples += bucket[i].samples;
				bucket[i].total_latency = 0;
				bucket[i].samples = 0;
			}

			if (tmp->samples >= 32) {
				int samples = tmp->samples;

				latency[rw] = tmp->total_latency;

				tmp->total_latency = 0;
				tmp->samples = 0;
				latency[rw] /= samples;
				if (latency[rw] == 0)
					continue;
				avg_latency[rw][i].latency = latency[rw];
			}
		}
	}

	for (rw = READ; rw <= WRITE; rw++) {
		for (i = 0; i < LATENCY_BUCKET_SIZE; i++) {
			if (!avg_latency[rw][i].latency) {
				if (td->avg_buckets[rw][i].latency < last_latency[rw])
					td->avg_buckets[rw][i].latency =
						last_latency[rw];
				continue;
			}

			if (!td->avg_buckets[rw][i].valid)
				latency[rw] = avg_latency[rw][i].latency;
			else
				latency[rw] = (td->avg_buckets[rw][i].latency * 7 +
					avg_latency[rw][i].latency) >> 3;

			td->avg_buckets[rw][i].latency = max(latency[rw],
				last_latency[rw]);
			td->avg_buckets[rw][i].valid = true;
			last_latency[rw] = td->avg_buckets[rw][i].latency;
		}
	}

	for (i = 0; i < LATENCY_BUCKET_SIZE; i++)
		throtl_log(&td->service_queue,
			"Latency bucket %d: read latency=%ld, read valid=%d, "
			"write latency=%ld, write valid=%d", i,
			td->avg_buckets[READ][i].latency,
			td->avg_buckets[READ][i].valid,
			td->avg_buckets[WRITE][i].latency,
			td->avg_buckets[WRITE][i].valid);
}
#else
static inline void throtl_update_latency_buckets(struct throtl_data *td)
{
}

static void blk_throtl_update_idletime(struct throtl_grp *tg)
{
}

static void throtl_downgrade_check(struct throtl_grp *tg)
{
}

static void throtl_upgrade_check(struct throtl_grp *tg)
{
}

static bool throtl_can_upgrade(struct throtl_data *td,
	struct throtl_grp *this_tg)
{
	return false;
}

static void throtl_upgrade_state(struct throtl_data *td)
{
}
#endif

bool __blk_throtl_bio(struct bio *bio)
{
	struct request_queue *q = bdev_get_queue(bio->bi_bdev);
	struct blkcg_gq *blkg = bio->bi_blkg;
	struct throtl_qnode *qn = NULL;
	struct throtl_grp *tg = blkg_to_tg(blkg);
	struct throtl_service_queue *sq;
	bool rw = bio_data_dir(bio);
	bool throttled = false;
	struct throtl_data *td = tg->td;

	rcu_read_lock();

	if (!cgroup_subsys_on_dfl(io_cgrp_subsys)) {
		blkg_rwstat_add(&tg->stat_bytes, bio->bi_opf,
				bio->bi_iter.bi_size);
		blkg_rwstat_add(&tg->stat_ios, bio->bi_opf, 1);
	}

	spin_lock_irq(&q->queue_lock);

	throtl_update_latency_buckets(td);

	blk_throtl_update_idletime(tg);

	sq = &tg->service_queue;

again:
	while (true) {
		if (tg->last_low_overflow_time[rw] == 0)
			tg->last_low_overflow_time[rw] = jiffies;
		throtl_downgrade_check(tg);
		throtl_upgrade_check(tg);
		/* throtl is FIFO - if bios are already queued, should queue */
		if (sq->nr_queued[rw])
			break;

		/* if above limits, break to queue */
		if (!tg_may_dispatch(tg, bio, NULL)) {
			tg->last_low_overflow_time[rw] = jiffies;
			if (throtl_can_upgrade(td, tg)) {
				throtl_upgrade_state(td);
				goto again;
			}
			break;
		}

		/* within limits, let's charge and dispatch directly */
		throtl_charge_bio(tg, bio);

		/*
		 * We need to trim slice even when bios are not being queued
		 * otherwise it might happen that a bio is not queued for
		 * a long time and slice keeps on extending and trim is not
		 * called for a long time. Now if limits are reduced suddenly
		 * we take into account all the IO dispatched so far at new
		 * low rate and * newly queued IO gets a really long dispatch
		 * time.
		 *
		 * So keep on trimming slice even if bio is not queued.
		 */
		throtl_trim_slice(tg, rw);

		/*
		 * @bio passed through this layer without being throttled.
		 * Climb up the ladder.  If we're already at the top, it
		 * can be executed directly.
		 */
		qn = &tg->qnode_on_parent[rw];
		sq = sq->parent_sq;
		tg = sq_to_tg(sq);
		if (!tg) {
			bio_set_flag(bio, BIO_BPS_THROTTLED);
			goto out_unlock;
		}
	}

	/* out-of-limit, queue to @tg */
	throtl_log(sq, "[%c] bio. bdisp=%llu sz=%u bps=%llu iodisp=%u iops=%u queued=%d/%d",
		   rw == READ ? 'R' : 'W',
		   tg->bytes_disp[rw], bio->bi_iter.bi_size,
		   tg_bps_limit(tg, rw),
		   tg->io_disp[rw], tg_iops_limit(tg, rw),
		   sq->nr_queued[READ], sq->nr_queued[WRITE]);

	tg->last_low_overflow_time[rw] = jiffies;

	td->nr_queued[rw]++;
	throtl_add_bio_tg(bio, qn, tg);
	throttled = true;

	/*
	 * Update @tg's dispatch time and force schedule dispatch if @tg
	 * was empty before @bio.  The forced scheduling isn't likely to
	 * cause undue delay as @bio is likely to be dispatched directly if
	 * its @tg's disptime is not in the future.
	 */
	if (tg->flags & THROTL_TG_WAS_EMPTY) {
		tg_update_disptime(tg);
		throtl_schedule_next_dispatch(tg->service_queue.parent_sq, true);
	}

out_unlock:
#ifdef CONFIG_BLK_DEV_THROTTLING_LOW
	if (throttled || !td->track_bio_latency)
		bio->bi_issue.value |= BIO_ISSUE_THROTL_SKIP_LATENCY;
#endif
	spin_unlock_irq(&q->queue_lock);

	rcu_read_unlock();
	return throttled;
}

#ifdef CONFIG_BLK_DEV_THROTTLING_LOW
static void throtl_track_latency(struct throtl_data *td, sector_t size,
				 enum req_op op, unsigned long time)
{
	const bool rw = op_is_write(op);
	struct latency_bucket *latency;
	int index;

	if (!td || td->limit_index != LIMIT_LOW ||
	    !(op == REQ_OP_READ || op == REQ_OP_WRITE) ||
	    !blk_queue_nonrot(td->queue))
		return;

	index = request_bucket_index(size);

	latency = get_cpu_ptr(td->latency_buckets[rw]);
	latency[index].total_latency += time;
	latency[index].samples++;
	put_cpu_ptr(td->latency_buckets[rw]);
}

void blk_throtl_stat_add(struct request *rq, u64 time_ns)
{
	struct request_queue *q = rq->q;
	struct throtl_data *td = q->td;

	throtl_track_latency(td, blk_rq_stats_sectors(rq), req_op(rq),
			     time_ns >> 10);
}

void blk_throtl_bio_endio(struct bio *bio)
{
	struct blkcg_gq *blkg;
	struct throtl_grp *tg;
	u64 finish_time_ns;
	unsigned long finish_time;
	unsigned long start_time;
	unsigned long lat;
	int rw = bio_data_dir(bio);

	blkg = bio->bi_blkg;
	if (!blkg)
		return;
	tg = blkg_to_tg(blkg);
	if (!tg->td->limit_valid[LIMIT_LOW])
		return;

	finish_time_ns = ktime_get_ns();
	tg->last_finish_time = finish_time_ns >> 10;

	start_time = bio_issue_time(&bio->bi_issue) >> 10;
	finish_time = __bio_issue_time(finish_time_ns) >> 10;
	if (!start_time || finish_time <= start_time)
		return;

	lat = finish_time - start_time;
	/* this is only for bio based driver */
	if (!(bio->bi_issue.value & BIO_ISSUE_THROTL_SKIP_LATENCY))
		throtl_track_latency(tg->td, bio_issue_size(&bio->bi_issue),
				     bio_op(bio), lat);

	if (tg->latency_target && lat >= tg->td->filtered_latency) {
		int bucket;
		unsigned int threshold;

		bucket = request_bucket_index(bio_issue_size(&bio->bi_issue));
		threshold = tg->td->avg_buckets[rw][bucket].latency +
			tg->latency_target;
		if (lat > threshold)
			tg->bad_bio_cnt++;
		/*
		 * Not race free, could get wrong count, which means cgroups
		 * will be throttled
		 */
		tg->bio_cnt++;
	}

	if (time_after(jiffies, tg->bio_cnt_reset_time) || tg->bio_cnt > 1024) {
		tg->bio_cnt_reset_time = tg->td->throtl_slice + jiffies;
		tg->bio_cnt /= 2;
		tg->bad_bio_cnt /= 2;
	}
}
#endif

int blk_throtl_init(struct gendisk *disk)
{
	struct request_queue *q = disk->queue;
	struct throtl_data *td;
	int ret;

	td = kzalloc_node(sizeof(*td), GFP_KERNEL, q->node);
	if (!td)
		return -ENOMEM;
	td->latency_buckets[READ] = __alloc_percpu(sizeof(struct latency_bucket) *
		LATENCY_BUCKET_SIZE, __alignof__(u64));
	if (!td->latency_buckets[READ]) {
		kfree(td);
		return -ENOMEM;
	}
	td->latency_buckets[WRITE] = __alloc_percpu(sizeof(struct latency_bucket) *
		LATENCY_BUCKET_SIZE, __alignof__(u64));
	if (!td->latency_buckets[WRITE]) {
		free_percpu(td->latency_buckets[READ]);
		kfree(td);
		return -ENOMEM;
	}

	INIT_WORK(&td->dispatch_work, blk_throtl_dispatch_work_fn);
	throtl_service_queue_init(&td->service_queue);

	q->td = td;
	td->queue = q;

	td->limit_valid[LIMIT_MAX] = true;
	td->limit_index = LIMIT_MAX;
	td->low_upgrade_time = jiffies;
	td->low_downgrade_time = jiffies;

	/* activate policy */
	ret = blkcg_activate_policy(disk, &blkcg_policy_throtl);
	if (ret) {
		free_percpu(td->latency_buckets[READ]);
		free_percpu(td->latency_buckets[WRITE]);
		kfree(td);
	}
	return ret;
}

void blk_throtl_exit(struct gendisk *disk)
{
	struct request_queue *q = disk->queue;

	BUG_ON(!q->td);
	del_timer_sync(&q->td->service_queue.pending_timer);
	throtl_shutdown_wq(q);
	blkcg_deactivate_policy(disk, &blkcg_policy_throtl);
	free_percpu(q->td->latency_buckets[READ]);
	free_percpu(q->td->latency_buckets[WRITE]);
	kfree(q->td);
}

void blk_throtl_register(struct gendisk *disk)
{
	struct request_queue *q = disk->queue;
	struct throtl_data *td;
	int i;

	td = q->td;
	BUG_ON(!td);

	if (blk_queue_nonrot(q)) {
		td->throtl_slice = DFL_THROTL_SLICE_SSD;
		td->filtered_latency = LATENCY_FILTERED_SSD;
	} else {
		td->throtl_slice = DFL_THROTL_SLICE_HD;
		td->filtered_latency = LATENCY_FILTERED_HD;
		for (i = 0; i < LATENCY_BUCKET_SIZE; i++) {
			td->avg_buckets[READ][i].latency = DFL_HD_BASELINE_LATENCY;
			td->avg_buckets[WRITE][i].latency = DFL_HD_BASELINE_LATENCY;
		}
	}
#ifndef CONFIG_BLK_DEV_THROTTLING_LOW
	/* if no low limit, use previous default */
	td->throtl_slice = DFL_THROTL_SLICE_HD;

#else
	td->track_bio_latency = !queue_is_mq(q);
	if (!td->track_bio_latency)
		blk_stat_enable_accounting(q);
#endif
}

#ifdef CONFIG_BLK_DEV_THROTTLING_LOW
ssize_t blk_throtl_sample_time_show(struct request_queue *q, char *page)
{
	if (!q->td)
		return -EINVAL;
	return sprintf(page, "%u\n", jiffies_to_msecs(q->td->throtl_slice));
}

ssize_t blk_throtl_sample_time_store(struct request_queue *q,
	const char *page, size_t count)
{
	unsigned long v;
	unsigned long t;

	if (!q->td)
		return -EINVAL;
	if (kstrtoul(page, 10, &v))
		return -EINVAL;
	t = msecs_to_jiffies(v);
	if (t == 0 || t > MAX_THROTL_SLICE)
		return -EINVAL;
	q->td->throtl_slice = t;
	return count;
}
#endif

static int __init throtl_init(void)
{
	kthrotld_workqueue = alloc_workqueue("kthrotld", WQ_MEM_RECLAIM, 0);
	if (!kthrotld_workqueue)
		panic("Failed to create kthrotld\n");

	return blkcg_policy_register(&blkcg_policy_throtl);
}

module_init(throtl_init);
