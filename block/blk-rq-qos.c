#include "blk-rq-qos.h"

/*
 * Increment 'v', if 'v' is below 'below'. Returns true if we succeeded,
 * false if 'v' + 1 would be bigger than 'below'.
 */
static bool atomic_inc_below(atomic_t *v, unsigned int below)
{
	unsigned int cur = atomic_read(v);

	for (;;) {
		unsigned int old;

		if (cur >= below)
			return false;
		old = atomic_cmpxchg(v, cur, cur + 1);
		if (old == cur)
			break;
		cur = old;
	}

	return true;
}

bool rq_wait_inc_below(struct rq_wait *rq_wait, unsigned int limit)
{
	return atomic_inc_below(&rq_wait->inflight, limit);
}

void rq_qos_cleanup(struct request_queue *q, struct bio *bio)
{
	struct rq_qos *rqos;

	for (rqos = q->rq_qos; rqos; rqos = rqos->next) {
		if (rqos->ops->cleanup)
			rqos->ops->cleanup(rqos, bio);
	}
}

void rq_qos_done(struct request_queue *q, struct request *rq)
{
	struct rq_qos *rqos;

	for (rqos = q->rq_qos; rqos; rqos = rqos->next) {
		if (rqos->ops->done)
			rqos->ops->done(rqos, rq);
	}
}

void rq_qos_issue(struct request_queue *q, struct request *rq)
{
	struct rq_qos *rqos;

	for(rqos = q->rq_qos; rqos; rqos = rqos->next) {
		if (rqos->ops->issue)
			rqos->ops->issue(rqos, rq);
	}
}

void rq_qos_requeue(struct request_queue *q, struct request *rq)
{
	struct rq_qos *rqos;

	for(rqos = q->rq_qos; rqos; rqos = rqos->next) {
		if (rqos->ops->requeue)
			rqos->ops->requeue(rqos, rq);
	}
}

void rq_qos_throttle(struct request_queue *q, struct bio *bio,
		     spinlock_t *lock)
{
	struct rq_qos *rqos;

	for(rqos = q->rq_qos; rqos; rqos = rqos->next) {
		if (rqos->ops->throttle)
			rqos->ops->throttle(rqos, bio, lock);
	}
}

void rq_qos_track(struct request_queue *q, struct request *rq, struct bio *bio)
{
	struct rq_qos *rqos;

	for(rqos = q->rq_qos; rqos; rqos = rqos->next) {
		if (rqos->ops->track)
			rqos->ops->track(rqos, rq, bio);
	}
}

void rq_qos_done_bio(struct request_queue *q, struct bio *bio)
{
	struct rq_qos *rqos;

	for(rqos = q->rq_qos; rqos; rqos = rqos->next) {
		if (rqos->ops->done_bio)
			rqos->ops->done_bio(rqos, bio);
	}
}

/*
 * Return true, if we can't increase the depth further by scaling
 */
bool rq_depth_calc_max_depth(struct rq_depth *rqd)
{
	unsigned int depth;
	bool ret = false;

	/*
	 * For QD=1 devices, this is a special case. It's important for those
	 * to have one request ready when one completes, so force a depth of
	 * 2 for those devices. On the backend, it'll be a depth of 1 anyway,
	 * since the device can't have more than that in flight. If we're
	 * scaling down, then keep a setting of 1/1/1.
	 */
	if (rqd->queue_depth == 1) {
		if (rqd->scale_step > 0)
			rqd->max_depth = 1;
		else {
			rqd->max_depth = 2;
			ret = true;
		}
	} else {
		/*
		 * scale_step == 0 is our default state. If we have suffered
		 * latency spikes, step will be > 0, and we shrink the
		 * allowed write depths. If step is < 0, we're only doing
		 * writes, and we allow a temporarily higher depth to
		 * increase performance.
		 */
		depth = min_t(unsigned int, rqd->default_depth,
			      rqd->queue_depth);
		if (rqd->scale_step > 0)
			depth = 1 + ((depth - 1) >> min(31, rqd->scale_step));
		else if (rqd->scale_step < 0) {
			unsigned int maxd = 3 * rqd->queue_depth / 4;

			depth = 1 + ((depth - 1) << -rqd->scale_step);
			if (depth > maxd) {
				depth = maxd;
				ret = true;
			}
		}

		rqd->max_depth = depth;
	}

	return ret;
}

/* Returns true on success and false if scaling up wasn't possible */
bool rq_depth_scale_up(struct rq_depth *rqd)
{
	/*
	 * Hit max in previous round, stop here
	 */
	if (rqd->scaled_max)
		return false;

	rqd->scale_step--;

	rqd->scaled_max = rq_depth_calc_max_depth(rqd);
	return true;
}

/*
 * Scale rwb down. If 'hard_throttle' is set, do it quicker, since we
 * had a latency violation. Returns true on success and returns false if
 * scaling down wasn't possible.
 */
bool rq_depth_scale_down(struct rq_depth *rqd, bool hard_throttle)
{
	/*
	 * Stop scaling down when we've hit the limit. This also prevents
	 * ->scale_step from going to crazy values, if the device can't
	 * keep up.
	 */
	if (rqd->max_depth == 1)
		return false;

	if (rqd->scale_step < 0 && hard_throttle)
		rqd->scale_step = 0;
	else
		rqd->scale_step++;

	rqd->scaled_max = false;
	rq_depth_calc_max_depth(rqd);
	return true;
}

void rq_qos_exit(struct request_queue *q)
{
	while (q->rq_qos) {
		struct rq_qos *rqos = q->rq_qos;
		q->rq_qos = rqos->next;
		rqos->ops->exit(rqos);
	}
}
