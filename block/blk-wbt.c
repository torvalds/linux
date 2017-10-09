/*
 * buffered writeback throttling. loosely based on CoDel. We can't drop
 * packets for IO scheduling, so the logic is something like this:
 *
 * - Monitor latencies in a defined window of time.
 * - If the minimum latency in the above window exceeds some target, increment
 *   scaling step and scale down queue depth by a factor of 2x. The monitoring
 *   window is then shrunk to 100 / sqrt(scaling step + 1).
 * - For any window where we don't have solid data on what the latencies
 *   look like, retain status quo.
 * - If latencies look good, decrement scaling step.
 * - If we're only doing writes, allow the scaling step to go negative. This
 *   will temporarily boost write performance, snapping back to a stable
 *   scaling step of 0 if reads show up or the heavy writers finish. Unlike
 *   positive scaling steps where we shrink the monitoring window, a negative
 *   scaling step retains the default step==0 window size.
 *
 * Copyright (C) 2016 Jens Axboe
 *
 */
#include <linux/kernel.h>
#include <linux/blk_types.h>
#include <linux/slab.h>
#include <linux/backing-dev.h>
#include <linux/swap.h>

#include "blk-wbt.h"

#define CREATE_TRACE_POINTS
#include <trace/events/wbt.h>

enum {
	/*
	 * Default setting, we'll scale up (to 75% of QD max) or down (min 1)
	 * from here depending on device stats
	 */
	RWB_DEF_DEPTH	= 16,

	/*
	 * 100msec window
	 */
	RWB_WINDOW_NSEC		= 100 * 1000 * 1000ULL,

	/*
	 * Disregard stats, if we don't meet this minimum
	 */
	RWB_MIN_WRITE_SAMPLES	= 3,

	/*
	 * If we have this number of consecutive windows with not enough
	 * information to scale up or down, scale up.
	 */
	RWB_UNKNOWN_BUMP	= 5,
};

static inline bool rwb_enabled(struct rq_wb *rwb)
{
	return rwb && rwb->wb_normal != 0;
}

/*
 * Increment 'v', if 'v' is below 'below'. Returns true if we succeeded,
 * false if 'v' + 1 would be bigger than 'below'.
 */
static bool atomic_inc_below(atomic_t *v, int below)
{
	int cur = atomic_read(v);

	for (;;) {
		int old;

		if (cur >= below)
			return false;
		old = atomic_cmpxchg(v, cur, cur + 1);
		if (old == cur)
			break;
		cur = old;
	}

	return true;
}

static void wb_timestamp(struct rq_wb *rwb, unsigned long *var)
{
	if (rwb_enabled(rwb)) {
		const unsigned long cur = jiffies;

		if (cur != *var)
			*var = cur;
	}
}

/*
 * If a task was rate throttled in balance_dirty_pages() within the last
 * second or so, use that to indicate a higher cleaning rate.
 */
static bool wb_recent_wait(struct rq_wb *rwb)
{
	struct bdi_writeback *wb = &rwb->queue->backing_dev_info->wb;

	return time_before(jiffies, wb->dirty_sleep + HZ);
}

static inline struct rq_wait *get_rq_wait(struct rq_wb *rwb, bool is_kswapd)
{
	return &rwb->rq_wait[is_kswapd];
}

static void rwb_wake_all(struct rq_wb *rwb)
{
	int i;

	for (i = 0; i < WBT_NUM_RWQ; i++) {
		struct rq_wait *rqw = &rwb->rq_wait[i];

		if (waitqueue_active(&rqw->wait))
			wake_up_all(&rqw->wait);
	}
}

void __wbt_done(struct rq_wb *rwb, enum wbt_flags wb_acct)
{
	struct rq_wait *rqw;
	int inflight, limit;

	if (!(wb_acct & WBT_TRACKED))
		return;

	rqw = get_rq_wait(rwb, wb_acct & WBT_KSWAPD);
	inflight = atomic_dec_return(&rqw->inflight);

	/*
	 * wbt got disabled with IO in flight. Wake up any potential
	 * waiters, we don't have to do more than that.
	 */
	if (unlikely(!rwb_enabled(rwb))) {
		rwb_wake_all(rwb);
		return;
	}

	/*
	 * If the device does write back caching, drop further down
	 * before we wake people up.
	 */
	if (rwb->wc && !wb_recent_wait(rwb))
		limit = 0;
	else
		limit = rwb->wb_normal;

	/*
	 * Don't wake anyone up if we are above the normal limit.
	 */
	if (inflight && inflight >= limit)
		return;

	if (waitqueue_active(&rqw->wait)) {
		int diff = limit - inflight;

		if (!inflight || diff >= rwb->wb_background / 2)
			wake_up_all(&rqw->wait);
	}
}

/*
 * Called on completion of a request. Note that it's also called when
 * a request is merged, when the request gets freed.
 */
void wbt_done(struct rq_wb *rwb, struct blk_issue_stat *stat)
{
	if (!rwb)
		return;

	if (!wbt_is_tracked(stat)) {
		if (rwb->sync_cookie == stat) {
			rwb->sync_issue = 0;
			rwb->sync_cookie = NULL;
		}

		if (wbt_is_read(stat))
			wb_timestamp(rwb, &rwb->last_comp);
		wbt_clear_state(stat);
	} else {
		WARN_ON_ONCE(stat == rwb->sync_cookie);
		__wbt_done(rwb, wbt_stat_to_mask(stat));
		wbt_clear_state(stat);
	}
}

/*
 * Return true, if we can't increase the depth further by scaling
 */
static bool calc_wb_limits(struct rq_wb *rwb)
{
	unsigned int depth;
	bool ret = false;

	if (!rwb->min_lat_nsec) {
		rwb->wb_max = rwb->wb_normal = rwb->wb_background = 0;
		return false;
	}

	/*
	 * For QD=1 devices, this is a special case. It's important for those
	 * to have one request ready when one completes, so force a depth of
	 * 2 for those devices. On the backend, it'll be a depth of 1 anyway,
	 * since the device can't have more than that in flight. If we're
	 * scaling down, then keep a setting of 1/1/1.
	 */
	if (rwb->queue_depth == 1) {
		if (rwb->scale_step > 0)
			rwb->wb_max = rwb->wb_normal = 1;
		else {
			rwb->wb_max = rwb->wb_normal = 2;
			ret = true;
		}
		rwb->wb_background = 1;
	} else {
		/*
		 * scale_step == 0 is our default state. If we have suffered
		 * latency spikes, step will be > 0, and we shrink the
		 * allowed write depths. If step is < 0, we're only doing
		 * writes, and we allow a temporarily higher depth to
		 * increase performance.
		 */
		depth = min_t(unsigned int, RWB_DEF_DEPTH, rwb->queue_depth);
		if (rwb->scale_step > 0)
			depth = 1 + ((depth - 1) >> min(31, rwb->scale_step));
		else if (rwb->scale_step < 0) {
			unsigned int maxd = 3 * rwb->queue_depth / 4;

			depth = 1 + ((depth - 1) << -rwb->scale_step);
			if (depth > maxd) {
				depth = maxd;
				ret = true;
			}
		}

		/*
		 * Set our max/normal/bg queue depths based on how far
		 * we have scaled down (->scale_step).
		 */
		rwb->wb_max = depth;
		rwb->wb_normal = (rwb->wb_max + 1) / 2;
		rwb->wb_background = (rwb->wb_max + 3) / 4;
	}

	return ret;
}

static inline bool stat_sample_valid(struct blk_rq_stat *stat)
{
	/*
	 * We need at least one read sample, and a minimum of
	 * RWB_MIN_WRITE_SAMPLES. We require some write samples to know
	 * that it's writes impacting us, and not just some sole read on
	 * a device that is in a lower power state.
	 */
	return (stat[READ].nr_samples >= 1 &&
		stat[WRITE].nr_samples >= RWB_MIN_WRITE_SAMPLES);
}

static u64 rwb_sync_issue_lat(struct rq_wb *rwb)
{
	u64 now, issue = ACCESS_ONCE(rwb->sync_issue);

	if (!issue || !rwb->sync_cookie)
		return 0;

	now = ktime_to_ns(ktime_get());
	return now - issue;
}

enum {
	LAT_OK = 1,
	LAT_UNKNOWN,
	LAT_UNKNOWN_WRITES,
	LAT_EXCEEDED,
};

static int latency_exceeded(struct rq_wb *rwb, struct blk_rq_stat *stat)
{
	struct backing_dev_info *bdi = rwb->queue->backing_dev_info;
	u64 thislat;

	/*
	 * If our stored sync issue exceeds the window size, or it
	 * exceeds our min target AND we haven't logged any entries,
	 * flag the latency as exceeded. wbt works off completion latencies,
	 * but for a flooded device, a single sync IO can take a long time
	 * to complete after being issued. If this time exceeds our
	 * monitoring window AND we didn't see any other completions in that
	 * window, then count that sync IO as a violation of the latency.
	 */
	thislat = rwb_sync_issue_lat(rwb);
	if (thislat > rwb->cur_win_nsec ||
	    (thislat > rwb->min_lat_nsec && !stat[READ].nr_samples)) {
		trace_wbt_lat(bdi, thislat);
		return LAT_EXCEEDED;
	}

	/*
	 * No read/write mix, if stat isn't valid
	 */
	if (!stat_sample_valid(stat)) {
		/*
		 * If we had writes in this stat window and the window is
		 * current, we're only doing writes. If a task recently
		 * waited or still has writes in flights, consider us doing
		 * just writes as well.
		 */
		if (stat[WRITE].nr_samples || wb_recent_wait(rwb) ||
		    wbt_inflight(rwb))
			return LAT_UNKNOWN_WRITES;
		return LAT_UNKNOWN;
	}

	/*
	 * If the 'min' latency exceeds our target, step down.
	 */
	if (stat[READ].min > rwb->min_lat_nsec) {
		trace_wbt_lat(bdi, stat[READ].min);
		trace_wbt_stat(bdi, stat);
		return LAT_EXCEEDED;
	}

	if (rwb->scale_step)
		trace_wbt_stat(bdi, stat);

	return LAT_OK;
}

static void rwb_trace_step(struct rq_wb *rwb, const char *msg)
{
	struct backing_dev_info *bdi = rwb->queue->backing_dev_info;

	trace_wbt_step(bdi, msg, rwb->scale_step, rwb->cur_win_nsec,
			rwb->wb_background, rwb->wb_normal, rwb->wb_max);
}

static void scale_up(struct rq_wb *rwb)
{
	/*
	 * Hit max in previous round, stop here
	 */
	if (rwb->scaled_max)
		return;

	rwb->scale_step--;
	rwb->unknown_cnt = 0;

	rwb->scaled_max = calc_wb_limits(rwb);

	rwb_wake_all(rwb);

	rwb_trace_step(rwb, "step up");
}

/*
 * Scale rwb down. If 'hard_throttle' is set, do it quicker, since we
 * had a latency violation.
 */
static void scale_down(struct rq_wb *rwb, bool hard_throttle)
{
	/*
	 * Stop scaling down when we've hit the limit. This also prevents
	 * ->scale_step from going to crazy values, if the device can't
	 * keep up.
	 */
	if (rwb->wb_max == 1)
		return;

	if (rwb->scale_step < 0 && hard_throttle)
		rwb->scale_step = 0;
	else
		rwb->scale_step++;

	rwb->scaled_max = false;
	rwb->unknown_cnt = 0;
	calc_wb_limits(rwb);
	rwb_trace_step(rwb, "step down");
}

static void rwb_arm_timer(struct rq_wb *rwb)
{
	if (rwb->scale_step > 0) {
		/*
		 * We should speed this up, using some variant of a fast
		 * integer inverse square root calculation. Since we only do
		 * this for every window expiration, it's not a huge deal,
		 * though.
		 */
		rwb->cur_win_nsec = div_u64(rwb->win_nsec << 4,
					int_sqrt((rwb->scale_step + 1) << 8));
	} else {
		/*
		 * For step < 0, we don't want to increase/decrease the
		 * window size.
		 */
		rwb->cur_win_nsec = rwb->win_nsec;
	}

	blk_stat_activate_nsecs(rwb->cb, rwb->cur_win_nsec);
}

static void wb_timer_fn(struct blk_stat_callback *cb)
{
	struct rq_wb *rwb = cb->data;
	unsigned int inflight = wbt_inflight(rwb);
	int status;

	status = latency_exceeded(rwb, cb->stat);

	trace_wbt_timer(rwb->queue->backing_dev_info, status, rwb->scale_step,
			inflight);

	/*
	 * If we exceeded the latency target, step down. If we did not,
	 * step one level up. If we don't know enough to say either exceeded
	 * or ok, then don't do anything.
	 */
	switch (status) {
	case LAT_EXCEEDED:
		scale_down(rwb, true);
		break;
	case LAT_OK:
		scale_up(rwb);
		break;
	case LAT_UNKNOWN_WRITES:
		/*
		 * We started a the center step, but don't have a valid
		 * read/write sample, but we do have writes going on.
		 * Allow step to go negative, to increase write perf.
		 */
		scale_up(rwb);
		break;
	case LAT_UNKNOWN:
		if (++rwb->unknown_cnt < RWB_UNKNOWN_BUMP)
			break;
		/*
		 * We get here when previously scaled reduced depth, and we
		 * currently don't have a valid read/write sample. For that
		 * case, slowly return to center state (step == 0).
		 */
		if (rwb->scale_step > 0)
			scale_up(rwb);
		else if (rwb->scale_step < 0)
			scale_down(rwb, false);
		break;
	default:
		break;
	}

	/*
	 * Re-arm timer, if we have IO in flight
	 */
	if (rwb->scale_step || inflight)
		rwb_arm_timer(rwb);
}

void wbt_update_limits(struct rq_wb *rwb)
{
	rwb->scale_step = 0;
	rwb->scaled_max = false;
	calc_wb_limits(rwb);

	rwb_wake_all(rwb);
}

static bool close_io(struct rq_wb *rwb)
{
	const unsigned long now = jiffies;

	return time_before(now, rwb->last_issue + HZ / 10) ||
		time_before(now, rwb->last_comp + HZ / 10);
}

#define REQ_HIPRIO	(REQ_SYNC | REQ_META | REQ_PRIO)

static inline unsigned int get_limit(struct rq_wb *rwb, unsigned long rw)
{
	unsigned int limit;

	/*
	 * At this point we know it's a buffered write. If this is
	 * kswapd trying to free memory, or REQ_SYNC is set, set, then
	 * it's WB_SYNC_ALL writeback, and we'll use the max limit for
	 * that. If the write is marked as a background write, then use
	 * the idle limit, or go to normal if we haven't had competing
	 * IO for a bit.
	 */
	if ((rw & REQ_HIPRIO) || wb_recent_wait(rwb) || current_is_kswapd())
		limit = rwb->wb_max;
	else if ((rw & REQ_BACKGROUND) || close_io(rwb)) {
		/*
		 * If less than 100ms since we completed unrelated IO,
		 * limit us to half the depth for background writeback.
		 */
		limit = rwb->wb_background;
	} else
		limit = rwb->wb_normal;

	return limit;
}

static inline bool may_queue(struct rq_wb *rwb, struct rq_wait *rqw,
			     wait_queue_entry_t *wait, unsigned long rw)
{
	/*
	 * inc it here even if disabled, since we'll dec it at completion.
	 * this only happens if the task was sleeping in __wbt_wait(),
	 * and someone turned it off at the same time.
	 */
	if (!rwb_enabled(rwb)) {
		atomic_inc(&rqw->inflight);
		return true;
	}

	/*
	 * If the waitqueue is already active and we are not the next
	 * in line to be woken up, wait for our turn.
	 */
	if (waitqueue_active(&rqw->wait) &&
	    rqw->wait.head.next != &wait->entry)
		return false;

	return atomic_inc_below(&rqw->inflight, get_limit(rwb, rw));
}

/*
 * Block if we will exceed our limit, or if we are currently waiting for
 * the timer to kick off queuing again.
 */
static void __wbt_wait(struct rq_wb *rwb, unsigned long rw, spinlock_t *lock)
	__releases(lock)
	__acquires(lock)
{
	struct rq_wait *rqw = get_rq_wait(rwb, current_is_kswapd());
	DEFINE_WAIT(wait);

	if (may_queue(rwb, rqw, &wait, rw))
		return;

	do {
		prepare_to_wait_exclusive(&rqw->wait, &wait,
						TASK_UNINTERRUPTIBLE);

		if (may_queue(rwb, rqw, &wait, rw))
			break;

		if (lock) {
			spin_unlock_irq(lock);
			io_schedule();
			spin_lock_irq(lock);
		} else
			io_schedule();
	} while (1);

	finish_wait(&rqw->wait, &wait);
}

static inline bool wbt_should_throttle(struct rq_wb *rwb, struct bio *bio)
{
	const int op = bio_op(bio);

	/*
	 * If not a WRITE, do nothing
	 */
	if (op != REQ_OP_WRITE)
		return false;

	/*
	 * Don't throttle WRITE_ODIRECT
	 */
	if ((bio->bi_opf & (REQ_SYNC | REQ_IDLE)) == (REQ_SYNC | REQ_IDLE))
		return false;

	return true;
}

/*
 * Returns true if the IO request should be accounted, false if not.
 * May sleep, if we have exceeded the writeback limits. Caller can pass
 * in an irq held spinlock, if it holds one when calling this function.
 * If we do sleep, we'll release and re-grab it.
 */
enum wbt_flags wbt_wait(struct rq_wb *rwb, struct bio *bio, spinlock_t *lock)
{
	unsigned int ret = 0;

	if (!rwb_enabled(rwb))
		return 0;

	if (bio_op(bio) == REQ_OP_READ)
		ret = WBT_READ;

	if (!wbt_should_throttle(rwb, bio)) {
		if (ret & WBT_READ)
			wb_timestamp(rwb, &rwb->last_issue);
		return ret;
	}

	__wbt_wait(rwb, bio->bi_opf, lock);

	if (!blk_stat_is_active(rwb->cb))
		rwb_arm_timer(rwb);

	if (current_is_kswapd())
		ret |= WBT_KSWAPD;

	return ret | WBT_TRACKED;
}

void wbt_issue(struct rq_wb *rwb, struct blk_issue_stat *stat)
{
	if (!rwb_enabled(rwb))
		return;

	/*
	 * Track sync issue, in case it takes a long time to complete. Allows
	 * us to react quicker, if a sync IO takes a long time to complete.
	 * Note that this is just a hint. 'stat' can go away when the
	 * request completes, so it's important we never dereference it. We
	 * only use the address to compare with, which is why we store the
	 * sync_issue time locally.
	 */
	if (wbt_is_read(stat) && !rwb->sync_issue) {
		rwb->sync_cookie = stat;
		rwb->sync_issue = blk_stat_time(stat);
	}
}

void wbt_requeue(struct rq_wb *rwb, struct blk_issue_stat *stat)
{
	if (!rwb_enabled(rwb))
		return;
	if (stat == rwb->sync_cookie) {
		rwb->sync_issue = 0;
		rwb->sync_cookie = NULL;
	}
}

void wbt_set_queue_depth(struct rq_wb *rwb, unsigned int depth)
{
	if (rwb) {
		rwb->queue_depth = depth;
		wbt_update_limits(rwb);
	}
}

void wbt_set_write_cache(struct rq_wb *rwb, bool write_cache_on)
{
	if (rwb)
		rwb->wc = write_cache_on;
}

/*
 * Disable wbt, if enabled by default.
 */
void wbt_disable_default(struct request_queue *q)
{
	struct rq_wb *rwb = q->rq_wb;

	if (rwb && rwb->enable_state == WBT_STATE_ON_DEFAULT)
		wbt_exit(q);
}
EXPORT_SYMBOL_GPL(wbt_disable_default);

/*
 * Enable wbt if defaults are configured that way
 */
void wbt_enable_default(struct request_queue *q)
{
	/* Throttling already enabled? */
	if (q->rq_wb)
		return;

	/* Queue not registered? Maybe shutting down... */
	if (!test_bit(QUEUE_FLAG_REGISTERED, &q->queue_flags))
		return;

	if ((q->mq_ops && IS_ENABLED(CONFIG_BLK_WBT_MQ)) ||
	    (q->request_fn && IS_ENABLED(CONFIG_BLK_WBT_SQ)))
		wbt_init(q);
}
EXPORT_SYMBOL_GPL(wbt_enable_default);

u64 wbt_default_latency_nsec(struct request_queue *q)
{
	/*
	 * We default to 2msec for non-rotational storage, and 75msec
	 * for rotational storage.
	 */
	if (blk_queue_nonrot(q))
		return 2000000ULL;
	else
		return 75000000ULL;
}

static int wbt_data_dir(const struct request *rq)
{
	return rq_data_dir(rq);
}

int wbt_init(struct request_queue *q)
{
	struct rq_wb *rwb;
	int i;

	BUILD_BUG_ON(WBT_NR_BITS > BLK_STAT_RES_BITS);

	rwb = kzalloc(sizeof(*rwb), GFP_KERNEL);
	if (!rwb)
		return -ENOMEM;

	rwb->cb = blk_stat_alloc_callback(wb_timer_fn, wbt_data_dir, 2, rwb);
	if (!rwb->cb) {
		kfree(rwb);
		return -ENOMEM;
	}

	for (i = 0; i < WBT_NUM_RWQ; i++) {
		atomic_set(&rwb->rq_wait[i].inflight, 0);
		init_waitqueue_head(&rwb->rq_wait[i].wait);
	}

	rwb->wc = 1;
	rwb->queue_depth = RWB_DEF_DEPTH;
	rwb->last_comp = rwb->last_issue = jiffies;
	rwb->queue = q;
	rwb->win_nsec = RWB_WINDOW_NSEC;
	rwb->enable_state = WBT_STATE_ON_DEFAULT;
	wbt_update_limits(rwb);

	/*
	 * Assign rwb and add the stats callback.
	 */
	q->rq_wb = rwb;
	blk_stat_add_callback(q, rwb->cb);

	rwb->min_lat_nsec = wbt_default_latency_nsec(q);

	wbt_set_queue_depth(rwb, blk_queue_depth(q));
	wbt_set_write_cache(rwb, test_bit(QUEUE_FLAG_WC, &q->queue_flags));

	return 0;
}

void wbt_exit(struct request_queue *q)
{
	struct rq_wb *rwb = q->rq_wb;

	if (rwb) {
		blk_stat_remove_callback(q, rwb->cb);
		blk_stat_free_callback(rwb->cb);
		q->rq_wb = NULL;
		kfree(rwb);
	}
}
