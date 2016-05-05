/*
 * buffered writeback throttling. losely based on CoDel. We can't drop
 * packets for IO scheduling, so the logic is something like this:
 *
 * - Monitor latencies in a defined window of time.
 * - If the minimum latency in the above window exceeds some target, increment
 *   scaling step and scale down queue depth by a factor of 2x. The monitoring
 *   window is then shrunk to 100 / sqrt(scaling step + 1).
 * - For any window where we don't have solid data on what the latencies
 *   look like, retain status quo.
 * - If latencies look good, decrement scaling step.
 *
 * Copyright (C) 2016 Jens Axboe
 *
 * Things that (may) need changing:
 *
 *	- Different scaling of background/normal/high priority writeback.
 *	  We may have to violate guarantees for max.
 *	- We can have mismatches between the stat window and our window.
 *
 */
#include <linux/kernel.h>
#include <linux/bio.h>
#include <linux/blkdev.h>
#include <trace/events/block.h>

#include "blk.h"
#include "blk-wb.h"
#include "blk-stat.h"

enum {
	/*
	 * Might need to be higher
	 */
	RWB_MAX_DEPTH	= 64,

	/*
	 * 100msec window
	 */
	RWB_WINDOW_NSEC		= 100 * 1000 * 1000ULL,

	/*
	 * Disregard stats, if we don't meet these minimums
	 */
	RWB_MIN_WRITE_SAMPLES	= 3,
	RWB_MIN_READ_SAMPLES	= 1,

	RWB_UNKNOWN_BUMP	= 5,

	/*
	 * Target min latencies, in nsecs
	 */
	RWB_ROT_LAT	= 75000000ULL,	/* 75 msec */
	RWB_NONROT_LAT	= 2000000ULL,	/*   2 msec */
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

void __blk_wb_done(struct rq_wb *rwb)
{
	int inflight, limit = rwb->wb_normal;

	/*
	 * If the device does write back caching, drop further down
	 * before we wake people up.
	 */
	if (test_bit(QUEUE_FLAG_WC, &rwb->q->queue_flags) &&
	    !atomic_read(rwb->bdp_wait))
		limit = 0;
	else
		limit = rwb->wb_normal;

	/*
	 * Don't wake anyone up if we are above the normal limit. If
	 * throttling got disabled (limit == 0) with waiters, ensure
	 * that we wake them up.
	 */
	inflight = atomic_dec_return(&rwb->inflight);
	if (limit && inflight >= limit) {
		if (!rwb->wb_max)
			wake_up_all(&rwb->wait);
		return;
	}

	if (waitqueue_active(&rwb->wait)) {
		int diff = limit - inflight;

		if (!inflight || diff >= rwb->wb_background / 2)
			wake_up_nr(&rwb->wait, 1);
	}
}

/*
 * Called on completion of a request. Note that it's also called when
 * a request is merged, when the request gets freed.
 */
void blk_wb_done(struct rq_wb *rwb, struct request *rq)
{
	if (!rwb)
		return;

	if (!(rq->cmd_flags & REQ_BUF_INFLIGHT)) {
		if (rwb->sync_cookie == rq) {
			rwb->sync_issue = 0;
			rwb->sync_cookie = NULL;
		}

		wb_timestamp(rwb, &rwb->last_comp);
	} else {
		WARN_ON_ONCE(rq == rwb->sync_cookie);
		__blk_wb_done(rwb);
		rq->cmd_flags &= ~REQ_BUF_INFLIGHT;
	}
}

static void calc_wb_limits(struct rq_wb *rwb)
{
	unsigned int depth;

	if (!rwb->min_lat_nsec) {
		rwb->wb_max = rwb->wb_normal = rwb->wb_background = 0;
		return;
	}

	depth = min_t(unsigned int, RWB_MAX_DEPTH, blk_queue_depth(rwb->q));

	/*
	 * Reduce max depth by 50%, and re-calculate normal/bg based on that
	 */
	rwb->wb_max = 1 + ((depth - 1) >> min(31U, rwb->scale_step));
	rwb->wb_normal = (rwb->wb_max + 1) / 2;
	rwb->wb_background = (rwb->wb_max + 3) / 4;
}

static bool inline stat_sample_valid(struct blk_rq_stat *stat)
{
	/*
	 * We need at least one read sample, and a minimum of
	 * RWB_MIN_WRITE_SAMPLES. We require some write samples to know
	 * that it's writes impacting us, and not just some sole read on
	 * a device that is in a lower power state.
	 */
	return stat[0].nr_samples >= 1 &&
		stat[1].nr_samples >= RWB_MIN_WRITE_SAMPLES;
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
	LAT_OK,
	LAT_UNKNOWN,
	LAT_EXCEEDED,
};

static int __latency_exceeded(struct rq_wb *rwb, struct blk_rq_stat *stat)
{
	u64 thislat;

	if (!stat_sample_valid(stat))
		return LAT_UNKNOWN;

	/*
	 * If the 'min' latency exceeds our target, step down.
	 */
	if (stat[0].min > rwb->min_lat_nsec) {
		trace_block_wb_lat(stat[0].min);
		trace_block_wb_stat(stat);
		return LAT_EXCEEDED;
	}

	/*
	 * If our stored sync issue exceeds the window size, or it
	 * exceeds our min target AND we haven't logged any entries,
	 * flag the latency as exceeded.
	 */
	thislat = rwb_sync_issue_lat(rwb);
	if (thislat > rwb->cur_win_nsec ||
	    (thislat > rwb->min_lat_nsec && !stat[0].nr_samples)) {
		trace_block_wb_lat(thislat);
		return LAT_EXCEEDED;
	}

	if (rwb->scale_step)
		trace_block_wb_stat(stat);

	return LAT_OK;
}

static int latency_exceeded(struct rq_wb *rwb)
{
	struct blk_rq_stat stat[2];

	blk_queue_stat_get(rwb->q, stat);

	return __latency_exceeded(rwb, stat);
}

static void rwb_trace_step(struct rq_wb *rwb, const char *msg)
{
	trace_block_wb_step(msg, rwb->scale_step, rwb->cur_win_nsec,
				rwb->wb_background, rwb->wb_normal, rwb->wb_max);
}

static void scale_up(struct rq_wb *rwb)
{
	/*
	 * If we're at 0, we can't go lower.
	 */
	if (!rwb->scale_step)
		return;

	rwb->scale_step--;
	rwb->unknown_cnt = 0;
	blk_stat_clear(rwb->q);
	calc_wb_limits(rwb);

	if (waitqueue_active(&rwb->wait))
		wake_up_all(&rwb->wait);

	rwb_trace_step(rwb, "step up");
}

static void scale_down(struct rq_wb *rwb)
{
	/*
	 * Stop scaling down when we've hit the limit. This also prevents
	 * ->scale_step from going to crazy values, if the device can't
	 * keep up.
	 */
	if (rwb->wb_max == 1)
		return;

	rwb->scale_step++;
	rwb->unknown_cnt = 0;
	blk_stat_clear(rwb->q);
	calc_wb_limits(rwb);
	rwb_trace_step(rwb, "step down");
}

static void rwb_arm_timer(struct rq_wb *rwb)
{
	unsigned long expires;

	/*
	 * We should speed this up, using some variant of a fast integer
	 * inverse square root calculation. Since we only do this for
	 * every window expiration, it's not a huge deal, though.
	 */
	rwb->cur_win_nsec = div_u64(rwb->win_nsec << 4,
					int_sqrt((rwb->scale_step + 1) << 8));
	expires = jiffies + nsecs_to_jiffies(rwb->cur_win_nsec);
	mod_timer(&rwb->window_timer, expires);
}

static void blk_wb_timer_fn(unsigned long data)
{
	struct rq_wb *rwb = (struct rq_wb *) data;
	int status;

	/*
	 * If we exceeded the latency target, step down. If we did not,
	 * step one level up. If we don't know enough to say either exceeded
	 * or ok, then don't do anything.
	 */
	status = latency_exceeded(rwb);
	switch (status) {
	case LAT_EXCEEDED:
		scale_down(rwb);
		break;
	case LAT_OK:
		scale_up(rwb);
		break;
	case LAT_UNKNOWN:
		/*
		 * We had no read samples, start bumping up the write
		 * depth slowly
		 */
		if (++rwb->unknown_cnt >= RWB_UNKNOWN_BUMP)
			scale_up(rwb);
		break;
	default:
		break;
	}

	/*
	 * Re-arm timer, if we have IO in flight
	 */
	if (rwb->scale_step || atomic_read(&rwb->inflight))
		rwb_arm_timer(rwb);
}

void blk_wb_update_limits(struct rq_wb *rwb)
{
	rwb->scale_step = 0;
	calc_wb_limits(rwb);

	if (waitqueue_active(&rwb->wait))
		wake_up_all(&rwb->wait);
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
	 * At this point we know it's a buffered write. If REQ_SYNC is
	 * set, then it's WB_SYNC_ALL writeback, and we'll use the max
	 * limit for that. If the write is marked as a background write,
	 * then use the idle limit, or go to normal if we haven't had
	 * competing IO for a bit.
	 */
	if ((rw & REQ_HIPRIO) || atomic_read(rwb->bdp_wait))
		limit = rwb->wb_max;
	else if ((rw & REQ_BG) || close_io(rwb)) {
		/*
		 * If less than 100ms since we completed unrelated IO,
		 * limit us to half the depth for background writeback.
		 */
		limit = rwb->wb_background;
	} else
		limit = rwb->wb_normal;

	return limit;
}

static inline bool may_queue(struct rq_wb *rwb, unsigned long rw)
{
	/*
	 * inc it here even if disabled, since we'll dec it at completion.
	 * this only happens if the task was sleeping in __blk_wb_wait(),
	 * and someone turned it off at the same time.
	 */
	if (!rwb_enabled(rwb)) {
		atomic_inc(&rwb->inflight);
		return true;
	}

	return atomic_inc_below(&rwb->inflight, get_limit(rwb, rw));
}

/*
 * Block if we will exceed our limit, or if we are currently waiting for
 * the timer to kick off queuing again.
 */
static void __blk_wb_wait(struct rq_wb *rwb, unsigned long rw, spinlock_t *lock)
{
	DEFINE_WAIT(wait);

	if (may_queue(rwb, rw))
		return;

	do {
		prepare_to_wait_exclusive(&rwb->wait, &wait,
						TASK_UNINTERRUPTIBLE);

		if (may_queue(rwb, rw))
			break;

		if (lock)
			spin_unlock_irq(lock);

		io_schedule();

		if (lock)
			spin_lock_irq(lock);
	} while (1);

	finish_wait(&rwb->wait, &wait);
}

/*
 * Returns true if the IO request should be accounted, false if not.
 * May sleep, if we have exceeded the writeback limits. Caller can pass
 * in an irq held spinlock, if it holds one when calling this function.
 * If we do sleep, we'll release and re-grab it.
 */
bool blk_wb_wait(struct rq_wb *rwb, struct bio *bio, spinlock_t *lock)
{
	/*
	 * If disabled, or not a WRITE (or a discard), do nothing
	 */
	if (!rwb_enabled(rwb) || !(bio->bi_rw & REQ_WRITE) ||
	    (bio->bi_rw & REQ_DISCARD))
		goto no_q;

	/*
	 * Don't throttle WRITE_ODIRECT
	 */
	if ((bio->bi_rw & (REQ_SYNC | REQ_NOIDLE)) == REQ_SYNC)
		goto no_q;

	__blk_wb_wait(rwb, bio->bi_rw, lock);

	if (!timer_pending(&rwb->window_timer))
		rwb_arm_timer(rwb);

	return true;

no_q:
	wb_timestamp(rwb, &rwb->last_issue);
	return false;
}

void blk_wb_issue(struct rq_wb *rwb, struct request *rq)
{
	if (!rwb_enabled(rwb))
		return;
	if (!(rq->cmd_flags & REQ_BUF_INFLIGHT) && !rwb->sync_issue) {
		rwb->sync_cookie = rq;
		rwb->sync_issue = rq->issue_time;
	}
}

void blk_wb_requeue(struct rq_wb *rwb, struct request *rq)
{
	if (!rwb_enabled(rwb))
		return;
	if (rq == rwb->sync_cookie) {
		rwb->sync_issue = 0;
		rwb->sync_cookie = NULL;
	}
}

void blk_wb_init(struct request_queue *q)
{
	struct rq_wb *rwb;

	/*
	 * If this fails, we don't get throttling
	 */
	rwb = kzalloc(sizeof(*rwb), GFP_KERNEL);
	if (!rwb)
		return;

	atomic_set(&rwb->inflight, 0);
	init_waitqueue_head(&rwb->wait);
	setup_timer(&rwb->window_timer, blk_wb_timer_fn, (unsigned long) rwb);
	rwb->last_comp = rwb->last_issue = jiffies;
	rwb->bdp_wait = &q->backing_dev_info.wb.dirty_sleeping;
	rwb->q = q;

	if (blk_queue_nonrot(q))
		rwb->min_lat_nsec = RWB_NONROT_LAT;
	else
		rwb->min_lat_nsec = RWB_ROT_LAT;

	rwb->win_nsec = RWB_WINDOW_NSEC;
	blk_wb_update_limits(rwb);
	q->rq_wb = rwb;
}

void blk_wb_exit(struct request_queue *q)
{
	struct rq_wb *rwb = q->rq_wb;

	if (rwb) {
		del_timer_sync(&rwb->window_timer);
		kfree(q->rq_wb);
		q->rq_wb = NULL;
	}
}
