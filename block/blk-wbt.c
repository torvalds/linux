// SPDX-License-Identifier: GPL-2.0
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
#include "blk-rq-qos.h"

#define CREATE_TRACE_POINTS
#include <trace/events/wbt.h>

static inline void wbt_clear_state(struct request *rq)
{
	rq->wbt_flags = 0;
}

static inline enum wbt_flags wbt_flags(struct request *rq)
{
	return rq->wbt_flags;
}

static inline bool wbt_is_tracked(struct request *rq)
{
	return rq->wbt_flags & WBT_TRACKED;
}

static inline bool wbt_is_read(struct request *rq)
{
	return rq->wbt_flags & WBT_READ;
}

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
	struct bdi_writeback *wb = &rwb->rqos.q->backing_dev_info->wb;

	return time_before(jiffies, wb->dirty_sleep + HZ);
}

static inline struct rq_wait *get_rq_wait(struct rq_wb *rwb,
					  enum wbt_flags wb_acct)
{
	if (wb_acct & WBT_KSWAPD)
		return &rwb->rq_wait[WBT_RWQ_KSWAPD];
	else if (wb_acct & WBT_DISCARD)
		return &rwb->rq_wait[WBT_RWQ_DISCARD];

	return &rwb->rq_wait[WBT_RWQ_BG];
}

static void rwb_wake_all(struct rq_wb *rwb)
{
	int i;

	for (i = 0; i < WBT_NUM_RWQ; i++) {
		struct rq_wait *rqw = &rwb->rq_wait[i];

		if (wq_has_sleeper(&rqw->wait))
			wake_up_all(&rqw->wait);
	}
}

static void wbt_rqw_done(struct rq_wb *rwb, struct rq_wait *rqw,
			 enum wbt_flags wb_acct)
{
	int inflight, limit;

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
	 * For discards, our limit is always the background. For writes, if
	 * the device does write back caching, drop further down before we
	 * wake people up.
	 */
	if (wb_acct & WBT_DISCARD)
		limit = rwb->wb_background;
	else if (rwb->wc && !wb_recent_wait(rwb))
		limit = 0;
	else
		limit = rwb->wb_normal;

	/*
	 * Don't wake anyone up if we are above the normal limit.
	 */
	if (inflight && inflight >= limit)
		return;

	if (wq_has_sleeper(&rqw->wait)) {
		int diff = limit - inflight;

		if (!inflight || diff >= rwb->wb_background / 2)
			wake_up_all(&rqw->wait);
	}
}

static void __wbt_done(struct rq_qos *rqos, enum wbt_flags wb_acct)
{
	struct rq_wb *rwb = RQWB(rqos);
	struct rq_wait *rqw;

	if (!(wb_acct & WBT_TRACKED))
		return;

	rqw = get_rq_wait(rwb, wb_acct);
	wbt_rqw_done(rwb, rqw, wb_acct);
}

/*
 * Called on completion of a request. Note that it's also called when
 * a request is merged, when the request gets freed.
 */
static void wbt_done(struct rq_qos *rqos, struct request *rq)
{
	struct rq_wb *rwb = RQWB(rqos);

	if (!wbt_is_tracked(rq)) {
		if (rwb->sync_cookie == rq) {
			rwb->sync_issue = 0;
			rwb->sync_cookie = NULL;
		}

		if (wbt_is_read(rq))
			wb_timestamp(rwb, &rwb->last_comp);
	} else {
		WARN_ON_ONCE(rq == rwb->sync_cookie);
		__wbt_done(rqos, wbt_flags(rq));
	}
	wbt_clear_state(rq);
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
	u64 now, issue = READ_ONCE(rwb->sync_issue);

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
	struct backing_dev_info *bdi = rwb->rqos.q->backing_dev_info;
	struct rq_depth *rqd = &rwb->rq_depth;
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

	if (rqd->scale_step)
		trace_wbt_stat(bdi, stat);

	return LAT_OK;
}

static void rwb_trace_step(struct rq_wb *rwb, const char *msg)
{
	struct backing_dev_info *bdi = rwb->rqos.q->backing_dev_info;
	struct rq_depth *rqd = &rwb->rq_depth;

	trace_wbt_step(bdi, msg, rqd->scale_step, rwb->cur_win_nsec,
			rwb->wb_background, rwb->wb_normal, rqd->max_depth);
}

static void calc_wb_limits(struct rq_wb *rwb)
{
	if (rwb->min_lat_nsec == 0) {
		rwb->wb_normal = rwb->wb_background = 0;
	} else if (rwb->rq_depth.max_depth <= 2) {
		rwb->wb_normal = rwb->rq_depth.max_depth;
		rwb->wb_background = 1;
	} else {
		rwb->wb_normal = (rwb->rq_depth.max_depth + 1) / 2;
		rwb->wb_background = (rwb->rq_depth.max_depth + 3) / 4;
	}
}

static void scale_up(struct rq_wb *rwb)
{
	if (!rq_depth_scale_up(&rwb->rq_depth))
		return;
	calc_wb_limits(rwb);
	rwb->unknown_cnt = 0;
	rwb_wake_all(rwb);
	rwb_trace_step(rwb, "scale up");
}

static void scale_down(struct rq_wb *rwb, bool hard_throttle)
{
	if (!rq_depth_scale_down(&rwb->rq_depth, hard_throttle))
		return;
	calc_wb_limits(rwb);
	rwb->unknown_cnt = 0;
	rwb_trace_step(rwb, "scale down");
}

static void rwb_arm_timer(struct rq_wb *rwb)
{
	struct rq_depth *rqd = &rwb->rq_depth;

	if (rqd->scale_step > 0) {
		/*
		 * We should speed this up, using some variant of a fast
		 * integer inverse square root calculation. Since we only do
		 * this for every window expiration, it's not a huge deal,
		 * though.
		 */
		rwb->cur_win_nsec = div_u64(rwb->win_nsec << 4,
					int_sqrt((rqd->scale_step + 1) << 8));
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
	struct rq_depth *rqd = &rwb->rq_depth;
	unsigned int inflight = wbt_inflight(rwb);
	int status;

	status = latency_exceeded(rwb, cb->stat);

	trace_wbt_timer(rwb->rqos.q->backing_dev_info, status, rqd->scale_step,
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
		if (rqd->scale_step > 0)
			scale_up(rwb);
		else if (rqd->scale_step < 0)
			scale_down(rwb, false);
		break;
	default:
		break;
	}

	/*
	 * Re-arm timer, if we have IO in flight
	 */
	if (rqd->scale_step || inflight)
		rwb_arm_timer(rwb);
}

static void __wbt_update_limits(struct rq_wb *rwb)
{
	struct rq_depth *rqd = &rwb->rq_depth;

	rqd->scale_step = 0;
	rqd->scaled_max = false;

	rq_depth_calc_max_depth(rqd);
	calc_wb_limits(rwb);

	rwb_wake_all(rwb);
}

void wbt_update_limits(struct request_queue *q)
{
	struct rq_qos *rqos = wbt_rq_qos(q);
	if (!rqos)
		return;
	__wbt_update_limits(RQWB(rqos));
}

u64 wbt_get_min_lat(struct request_queue *q)
{
	struct rq_qos *rqos = wbt_rq_qos(q);
	if (!rqos)
		return 0;
	return RQWB(rqos)->min_lat_nsec;
}

void wbt_set_min_lat(struct request_queue *q, u64 val)
{
	struct rq_qos *rqos = wbt_rq_qos(q);
	if (!rqos)
		return;
	RQWB(rqos)->min_lat_nsec = val;
	RQWB(rqos)->enable_state = WBT_STATE_ON_MANUAL;
	__wbt_update_limits(RQWB(rqos));
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
	 * If we got disabled, just return UINT_MAX. This ensures that
	 * we'll properly inc a new IO, and dec+wakeup at the end.
	 */
	if (!rwb_enabled(rwb))
		return UINT_MAX;

	if ((rw & REQ_OP_MASK) == REQ_OP_DISCARD)
		return rwb->wb_background;

	/*
	 * At this point we know it's a buffered write. If this is
	 * kswapd trying to free memory, or REQ_SYNC is set, then
	 * it's WB_SYNC_ALL writeback, and we'll use the max limit for
	 * that. If the write is marked as a background write, then use
	 * the idle limit, or go to normal if we haven't had competing
	 * IO for a bit.
	 */
	if ((rw & REQ_HIPRIO) || wb_recent_wait(rwb) || current_is_kswapd())
		limit = rwb->rq_depth.max_depth;
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

struct wbt_wait_data {
	struct rq_wb *rwb;
	enum wbt_flags wb_acct;
	unsigned long rw;
};

static bool wbt_inflight_cb(struct rq_wait *rqw, void *private_data)
{
	struct wbt_wait_data *data = private_data;
	return rq_wait_inc_below(rqw, get_limit(data->rwb, data->rw));
}

static void wbt_cleanup_cb(struct rq_wait *rqw, void *private_data)
{
	struct wbt_wait_data *data = private_data;
	wbt_rqw_done(data->rwb, rqw, data->wb_acct);
}

/*
 * Block if we will exceed our limit, or if we are currently waiting for
 * the timer to kick off queuing again.
 */
static void __wbt_wait(struct rq_wb *rwb, enum wbt_flags wb_acct,
		       unsigned long rw)
{
	struct rq_wait *rqw = get_rq_wait(rwb, wb_acct);
	struct wbt_wait_data data = {
		.rwb = rwb,
		.wb_acct = wb_acct,
		.rw = rw,
	};

	rq_qos_wait(rqw, &data, wbt_inflight_cb, wbt_cleanup_cb);
}

static inline bool wbt_should_throttle(struct rq_wb *rwb, struct bio *bio)
{
	switch (bio_op(bio)) {
	case REQ_OP_WRITE:
		/*
		 * Don't throttle WRITE_ODIRECT
		 */
		if ((bio->bi_opf & (REQ_SYNC | REQ_IDLE)) ==
		    (REQ_SYNC | REQ_IDLE))
			return false;
		/* fallthrough */
	case REQ_OP_DISCARD:
		return true;
	default:
		return false;
	}
}

static enum wbt_flags bio_to_wbt_flags(struct rq_wb *rwb, struct bio *bio)
{
	enum wbt_flags flags = 0;

	if (!rwb_enabled(rwb))
		return 0;

	if (bio_op(bio) == REQ_OP_READ) {
		flags = WBT_READ;
	} else if (wbt_should_throttle(rwb, bio)) {
		if (current_is_kswapd())
			flags |= WBT_KSWAPD;
		if (bio_op(bio) == REQ_OP_DISCARD)
			flags |= WBT_DISCARD;
		flags |= WBT_TRACKED;
	}
	return flags;
}

static void wbt_cleanup(struct rq_qos *rqos, struct bio *bio)
{
	struct rq_wb *rwb = RQWB(rqos);
	enum wbt_flags flags = bio_to_wbt_flags(rwb, bio);
	__wbt_done(rqos, flags);
}

/*
 * Returns true if the IO request should be accounted, false if not.
 * May sleep, if we have exceeded the writeback limits. Caller can pass
 * in an irq held spinlock, if it holds one when calling this function.
 * If we do sleep, we'll release and re-grab it.
 */
static void wbt_wait(struct rq_qos *rqos, struct bio *bio)
{
	struct rq_wb *rwb = RQWB(rqos);
	enum wbt_flags flags;

	flags = bio_to_wbt_flags(rwb, bio);
	if (!(flags & WBT_TRACKED)) {
		if (flags & WBT_READ)
			wb_timestamp(rwb, &rwb->last_issue);
		return;
	}

	__wbt_wait(rwb, flags, bio->bi_opf);

	if (!blk_stat_is_active(rwb->cb))
		rwb_arm_timer(rwb);
}

static void wbt_track(struct rq_qos *rqos, struct request *rq, struct bio *bio)
{
	struct rq_wb *rwb = RQWB(rqos);
	rq->wbt_flags |= bio_to_wbt_flags(rwb, bio);
}

static void wbt_issue(struct rq_qos *rqos, struct request *rq)
{
	struct rq_wb *rwb = RQWB(rqos);

	if (!rwb_enabled(rwb))
		return;

	/*
	 * Track sync issue, in case it takes a long time to complete. Allows us
	 * to react quicker, if a sync IO takes a long time to complete. Note
	 * that this is just a hint. The request can go away when it completes,
	 * so it's important we never dereference it. We only use the address to
	 * compare with, which is why we store the sync_issue time locally.
	 */
	if (wbt_is_read(rq) && !rwb->sync_issue) {
		rwb->sync_cookie = rq;
		rwb->sync_issue = rq->io_start_time_ns;
	}
}

static void wbt_requeue(struct rq_qos *rqos, struct request *rq)
{
	struct rq_wb *rwb = RQWB(rqos);
	if (!rwb_enabled(rwb))
		return;
	if (rq == rwb->sync_cookie) {
		rwb->sync_issue = 0;
		rwb->sync_cookie = NULL;
	}
}

void wbt_set_write_cache(struct request_queue *q, bool write_cache_on)
{
	struct rq_qos *rqos = wbt_rq_qos(q);
	if (rqos)
		RQWB(rqos)->wc = write_cache_on;
}

/*
 * Enable wbt if defaults are configured that way
 */
void wbt_enable_default(struct request_queue *q)
{
	struct rq_qos *rqos = wbt_rq_qos(q);
	/* Throttling already enabled? */
	if (rqos)
		return;

	/* Queue not registered? Maybe shutting down... */
	if (!blk_queue_registered(q))
		return;

	if (queue_is_mq(q) && IS_ENABLED(CONFIG_BLK_WBT_MQ))
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
	const int op = req_op(rq);

	if (op == REQ_OP_READ)
		return READ;
	else if (op_is_write(op))
		return WRITE;

	/* don't account */
	return -1;
}

static void wbt_queue_depth_changed(struct rq_qos *rqos)
{
	RQWB(rqos)->rq_depth.queue_depth = blk_queue_depth(rqos->q);
	__wbt_update_limits(RQWB(rqos));
}

static void wbt_exit(struct rq_qos *rqos)
{
	struct rq_wb *rwb = RQWB(rqos);
	struct request_queue *q = rqos->q;

	blk_stat_remove_callback(q, rwb->cb);
	blk_stat_free_callback(rwb->cb);
	kfree(rwb);
}

/*
 * Disable wbt, if enabled by default.
 */
void wbt_disable_default(struct request_queue *q)
{
	struct rq_qos *rqos = wbt_rq_qos(q);
	struct rq_wb *rwb;
	if (!rqos)
		return;
	rwb = RQWB(rqos);
	if (rwb->enable_state == WBT_STATE_ON_DEFAULT) {
		blk_stat_deactivate(rwb->cb);
		rwb->wb_normal = 0;
	}
}
EXPORT_SYMBOL_GPL(wbt_disable_default);

#ifdef CONFIG_BLK_DEBUG_FS
static int wbt_curr_win_nsec_show(void *data, struct seq_file *m)
{
	struct rq_qos *rqos = data;
	struct rq_wb *rwb = RQWB(rqos);

	seq_printf(m, "%llu\n", rwb->cur_win_nsec);
	return 0;
}

static int wbt_enabled_show(void *data, struct seq_file *m)
{
	struct rq_qos *rqos = data;
	struct rq_wb *rwb = RQWB(rqos);

	seq_printf(m, "%d\n", rwb->enable_state);
	return 0;
}

static int wbt_id_show(void *data, struct seq_file *m)
{
	struct rq_qos *rqos = data;

	seq_printf(m, "%u\n", rqos->id);
	return 0;
}

static int wbt_inflight_show(void *data, struct seq_file *m)
{
	struct rq_qos *rqos = data;
	struct rq_wb *rwb = RQWB(rqos);
	int i;

	for (i = 0; i < WBT_NUM_RWQ; i++)
		seq_printf(m, "%d: inflight %d\n", i,
			   atomic_read(&rwb->rq_wait[i].inflight));
	return 0;
}

static int wbt_min_lat_nsec_show(void *data, struct seq_file *m)
{
	struct rq_qos *rqos = data;
	struct rq_wb *rwb = RQWB(rqos);

	seq_printf(m, "%lu\n", rwb->min_lat_nsec);
	return 0;
}

static int wbt_unknown_cnt_show(void *data, struct seq_file *m)
{
	struct rq_qos *rqos = data;
	struct rq_wb *rwb = RQWB(rqos);

	seq_printf(m, "%u\n", rwb->unknown_cnt);
	return 0;
}

static int wbt_normal_show(void *data, struct seq_file *m)
{
	struct rq_qos *rqos = data;
	struct rq_wb *rwb = RQWB(rqos);

	seq_printf(m, "%u\n", rwb->wb_normal);
	return 0;
}

static int wbt_background_show(void *data, struct seq_file *m)
{
	struct rq_qos *rqos = data;
	struct rq_wb *rwb = RQWB(rqos);

	seq_printf(m, "%u\n", rwb->wb_background);
	return 0;
}

static const struct blk_mq_debugfs_attr wbt_debugfs_attrs[] = {
	{"curr_win_nsec", 0400, wbt_curr_win_nsec_show},
	{"enabled", 0400, wbt_enabled_show},
	{"id", 0400, wbt_id_show},
	{"inflight", 0400, wbt_inflight_show},
	{"min_lat_nsec", 0400, wbt_min_lat_nsec_show},
	{"unknown_cnt", 0400, wbt_unknown_cnt_show},
	{"wb_normal", 0400, wbt_normal_show},
	{"wb_background", 0400, wbt_background_show},
	{},
};
#endif

static struct rq_qos_ops wbt_rqos_ops = {
	.throttle = wbt_wait,
	.issue = wbt_issue,
	.track = wbt_track,
	.requeue = wbt_requeue,
	.done = wbt_done,
	.cleanup = wbt_cleanup,
	.queue_depth_changed = wbt_queue_depth_changed,
	.exit = wbt_exit,
#ifdef CONFIG_BLK_DEBUG_FS
	.debugfs_attrs = wbt_debugfs_attrs,
#endif
};

int wbt_init(struct request_queue *q)
{
	struct rq_wb *rwb;
	int i;

	rwb = kzalloc(sizeof(*rwb), GFP_KERNEL);
	if (!rwb)
		return -ENOMEM;

	rwb->cb = blk_stat_alloc_callback(wb_timer_fn, wbt_data_dir, 2, rwb);
	if (!rwb->cb) {
		kfree(rwb);
		return -ENOMEM;
	}

	for (i = 0; i < WBT_NUM_RWQ; i++)
		rq_wait_init(&rwb->rq_wait[i]);

	rwb->rqos.id = RQ_QOS_WBT;
	rwb->rqos.ops = &wbt_rqos_ops;
	rwb->rqos.q = q;
	rwb->last_comp = rwb->last_issue = jiffies;
	rwb->win_nsec = RWB_WINDOW_NSEC;
	rwb->enable_state = WBT_STATE_ON_DEFAULT;
	rwb->wc = 1;
	rwb->rq_depth.default_depth = RWB_DEF_DEPTH;
	__wbt_update_limits(rwb);

	/*
	 * Assign rwb and add the stats callback.
	 */
	rq_qos_add(q, &rwb->rqos);
	blk_stat_add_callback(q, rwb->cb);

	rwb->min_lat_nsec = wbt_default_latency_nsec(q);

	wbt_queue_depth_changed(&rwb->rqos);
	wbt_set_write_cache(q, test_bit(QUEUE_FLAG_WC, &q->queue_flags));

	return 0;
}
