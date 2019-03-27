/*
 * Copyright (c) 2007-2012 Niels Provos and Nick Mathewson
 * Copyright (c) 2002-2006 Niels Provos <provos@citi.umich.edu>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include "evconfig-private.h"

#include <sys/types.h>
#include <limits.h>
#include <string.h>
#include <stdlib.h>

#include "event2/event.h"
#include "event2/event_struct.h"
#include "event2/util.h"
#include "event2/bufferevent.h"
#include "event2/bufferevent_struct.h"
#include "event2/buffer.h"

#include "ratelim-internal.h"

#include "bufferevent-internal.h"
#include "mm-internal.h"
#include "util-internal.h"
#include "event-internal.h"

int
ev_token_bucket_init_(struct ev_token_bucket *bucket,
    const struct ev_token_bucket_cfg *cfg,
    ev_uint32_t current_tick,
    int reinitialize)
{
	if (reinitialize) {
		/* on reinitialization, we only clip downwards, since we've
		   already used who-knows-how-much bandwidth this tick.  We
		   leave "last_updated" as it is; the next update will add the
		   appropriate amount of bandwidth to the bucket.
		*/
		if (bucket->read_limit > (ev_int64_t) cfg->read_maximum)
			bucket->read_limit = cfg->read_maximum;
		if (bucket->write_limit > (ev_int64_t) cfg->write_maximum)
			bucket->write_limit = cfg->write_maximum;
	} else {
		bucket->read_limit = cfg->read_rate;
		bucket->write_limit = cfg->write_rate;
		bucket->last_updated = current_tick;
	}
	return 0;
}

int
ev_token_bucket_update_(struct ev_token_bucket *bucket,
    const struct ev_token_bucket_cfg *cfg,
    ev_uint32_t current_tick)
{
	/* It's okay if the tick number overflows, since we'll just
	 * wrap around when we do the unsigned substraction. */
	unsigned n_ticks = current_tick - bucket->last_updated;

	/* Make sure some ticks actually happened, and that time didn't
	 * roll back. */
	if (n_ticks == 0 || n_ticks > INT_MAX)
		return 0;

	/* Naively, we would say
		bucket->limit += n_ticks * cfg->rate;

		if (bucket->limit > cfg->maximum)
			bucket->limit = cfg->maximum;

	   But we're worried about overflow, so we do it like this:
	*/

	if ((cfg->read_maximum - bucket->read_limit) / n_ticks < cfg->read_rate)
		bucket->read_limit = cfg->read_maximum;
	else
		bucket->read_limit += n_ticks * cfg->read_rate;


	if ((cfg->write_maximum - bucket->write_limit) / n_ticks < cfg->write_rate)
		bucket->write_limit = cfg->write_maximum;
	else
		bucket->write_limit += n_ticks * cfg->write_rate;


	bucket->last_updated = current_tick;

	return 1;
}

static inline void
bufferevent_update_buckets(struct bufferevent_private *bev)
{
	/* Must hold lock on bev. */
	struct timeval now;
	unsigned tick;
	event_base_gettimeofday_cached(bev->bev.ev_base, &now);
	tick = ev_token_bucket_get_tick_(&now, bev->rate_limiting->cfg);
	if (tick != bev->rate_limiting->limit.last_updated)
		ev_token_bucket_update_(&bev->rate_limiting->limit,
		    bev->rate_limiting->cfg, tick);
}

ev_uint32_t
ev_token_bucket_get_tick_(const struct timeval *tv,
    const struct ev_token_bucket_cfg *cfg)
{
	/* This computation uses two multiplies and a divide.  We could do
	 * fewer if we knew that the tick length was an integer number of
	 * seconds, or if we knew it divided evenly into a second.  We should
	 * investigate that more.
	 */

	/* We cast to an ev_uint64_t first, since we don't want to overflow
	 * before we do the final divide. */
	ev_uint64_t msec = (ev_uint64_t)tv->tv_sec * 1000 + tv->tv_usec / 1000;
	return (unsigned)(msec / cfg->msec_per_tick);
}

struct ev_token_bucket_cfg *
ev_token_bucket_cfg_new(size_t read_rate, size_t read_burst,
    size_t write_rate, size_t write_burst,
    const struct timeval *tick_len)
{
	struct ev_token_bucket_cfg *r;
	struct timeval g;
	if (! tick_len) {
		g.tv_sec = 1;
		g.tv_usec = 0;
		tick_len = &g;
	}
	if (read_rate > read_burst || write_rate > write_burst ||
	    read_rate < 1 || write_rate < 1)
		return NULL;
	if (read_rate > EV_RATE_LIMIT_MAX ||
	    write_rate > EV_RATE_LIMIT_MAX ||
	    read_burst > EV_RATE_LIMIT_MAX ||
	    write_burst > EV_RATE_LIMIT_MAX)
		return NULL;
	r = mm_calloc(1, sizeof(struct ev_token_bucket_cfg));
	if (!r)
		return NULL;
	r->read_rate = read_rate;
	r->write_rate = write_rate;
	r->read_maximum = read_burst;
	r->write_maximum = write_burst;
	memcpy(&r->tick_timeout, tick_len, sizeof(struct timeval));
	r->msec_per_tick = (tick_len->tv_sec * 1000) +
	    (tick_len->tv_usec & COMMON_TIMEOUT_MICROSECONDS_MASK)/1000;
	return r;
}

void
ev_token_bucket_cfg_free(struct ev_token_bucket_cfg *cfg)
{
	mm_free(cfg);
}

/* Default values for max_single_read & max_single_write variables. */
#define MAX_SINGLE_READ_DEFAULT 16384
#define MAX_SINGLE_WRITE_DEFAULT 16384

#define LOCK_GROUP(g) EVLOCK_LOCK((g)->lock, 0)
#define UNLOCK_GROUP(g) EVLOCK_UNLOCK((g)->lock, 0)

static int bev_group_suspend_reading_(struct bufferevent_rate_limit_group *g);
static int bev_group_suspend_writing_(struct bufferevent_rate_limit_group *g);
static void bev_group_unsuspend_reading_(struct bufferevent_rate_limit_group *g);
static void bev_group_unsuspend_writing_(struct bufferevent_rate_limit_group *g);

/** Helper: figure out the maximum amount we should write if is_write, or
    the maximum amount we should read if is_read.  Return that maximum, or
    0 if our bucket is wholly exhausted.
 */
static inline ev_ssize_t
bufferevent_get_rlim_max_(struct bufferevent_private *bev, int is_write)
{
	/* needs lock on bev. */
	ev_ssize_t max_so_far = is_write?bev->max_single_write:bev->max_single_read;

#define LIM(x)						\
	(is_write ? (x).write_limit : (x).read_limit)

#define GROUP_SUSPENDED(g)			\
	(is_write ? (g)->write_suspended : (g)->read_suspended)

	/* Sets max_so_far to MIN(x, max_so_far) */
#define CLAMPTO(x)				\
	do {					\
		if (max_so_far > (x))		\
			max_so_far = (x);	\
	} while (0);

	if (!bev->rate_limiting)
		return max_so_far;

	/* If rate-limiting is enabled at all, update the appropriate
	   bucket, and take the smaller of our rate limit and the group
	   rate limit.
	 */

	if (bev->rate_limiting->cfg) {
		bufferevent_update_buckets(bev);
		max_so_far = LIM(bev->rate_limiting->limit);
	}
	if (bev->rate_limiting->group) {
		struct bufferevent_rate_limit_group *g =
		    bev->rate_limiting->group;
		ev_ssize_t share;
		LOCK_GROUP(g);
		if (GROUP_SUSPENDED(g)) {
			/* We can get here if we failed to lock this
			 * particular bufferevent while suspending the whole
			 * group. */
			if (is_write)
				bufferevent_suspend_write_(&bev->bev,
				    BEV_SUSPEND_BW_GROUP);
			else
				bufferevent_suspend_read_(&bev->bev,
				    BEV_SUSPEND_BW_GROUP);
			share = 0;
		} else {
			/* XXXX probably we should divide among the active
			 * members, not the total members. */
			share = LIM(g->rate_limit) / g->n_members;
			if (share < g->min_share)
				share = g->min_share;
		}
		UNLOCK_GROUP(g);
		CLAMPTO(share);
	}

	if (max_so_far < 0)
		max_so_far = 0;
	return max_so_far;
}

ev_ssize_t
bufferevent_get_read_max_(struct bufferevent_private *bev)
{
	return bufferevent_get_rlim_max_(bev, 0);
}

ev_ssize_t
bufferevent_get_write_max_(struct bufferevent_private *bev)
{
	return bufferevent_get_rlim_max_(bev, 1);
}

int
bufferevent_decrement_read_buckets_(struct bufferevent_private *bev, ev_ssize_t bytes)
{
	/* XXXXX Make sure all users of this function check its return value */
	int r = 0;
	/* need to hold lock on bev */
	if (!bev->rate_limiting)
		return 0;

	if (bev->rate_limiting->cfg) {
		bev->rate_limiting->limit.read_limit -= bytes;
		if (bev->rate_limiting->limit.read_limit <= 0) {
			bufferevent_suspend_read_(&bev->bev, BEV_SUSPEND_BW);
			if (event_add(&bev->rate_limiting->refill_bucket_event,
				&bev->rate_limiting->cfg->tick_timeout) < 0)
				r = -1;
		} else if (bev->read_suspended & BEV_SUSPEND_BW) {
			if (!(bev->write_suspended & BEV_SUSPEND_BW))
				event_del(&bev->rate_limiting->refill_bucket_event);
			bufferevent_unsuspend_read_(&bev->bev, BEV_SUSPEND_BW);
		}
	}

	if (bev->rate_limiting->group) {
		LOCK_GROUP(bev->rate_limiting->group);
		bev->rate_limiting->group->rate_limit.read_limit -= bytes;
		bev->rate_limiting->group->total_read += bytes;
		if (bev->rate_limiting->group->rate_limit.read_limit <= 0) {
			bev_group_suspend_reading_(bev->rate_limiting->group);
		} else if (bev->rate_limiting->group->read_suspended) {
			bev_group_unsuspend_reading_(bev->rate_limiting->group);
		}
		UNLOCK_GROUP(bev->rate_limiting->group);
	}

	return r;
}

int
bufferevent_decrement_write_buckets_(struct bufferevent_private *bev, ev_ssize_t bytes)
{
	/* XXXXX Make sure all users of this function check its return value */
	int r = 0;
	/* need to hold lock */
	if (!bev->rate_limiting)
		return 0;

	if (bev->rate_limiting->cfg) {
		bev->rate_limiting->limit.write_limit -= bytes;
		if (bev->rate_limiting->limit.write_limit <= 0) {
			bufferevent_suspend_write_(&bev->bev, BEV_SUSPEND_BW);
			if (event_add(&bev->rate_limiting->refill_bucket_event,
				&bev->rate_limiting->cfg->tick_timeout) < 0)
				r = -1;
		} else if (bev->write_suspended & BEV_SUSPEND_BW) {
			if (!(bev->read_suspended & BEV_SUSPEND_BW))
				event_del(&bev->rate_limiting->refill_bucket_event);
			bufferevent_unsuspend_write_(&bev->bev, BEV_SUSPEND_BW);
		}
	}

	if (bev->rate_limiting->group) {
		LOCK_GROUP(bev->rate_limiting->group);
		bev->rate_limiting->group->rate_limit.write_limit -= bytes;
		bev->rate_limiting->group->total_written += bytes;
		if (bev->rate_limiting->group->rate_limit.write_limit <= 0) {
			bev_group_suspend_writing_(bev->rate_limiting->group);
		} else if (bev->rate_limiting->group->write_suspended) {
			bev_group_unsuspend_writing_(bev->rate_limiting->group);
		}
		UNLOCK_GROUP(bev->rate_limiting->group);
	}

	return r;
}

/** Stop reading on every bufferevent in <b>g</b> */
static int
bev_group_suspend_reading_(struct bufferevent_rate_limit_group *g)
{
	/* Needs group lock */
	struct bufferevent_private *bev;
	g->read_suspended = 1;
	g->pending_unsuspend_read = 0;

	/* Note that in this loop we call EVLOCK_TRY_LOCK_ instead of BEV_LOCK,
	   to prevent a deadlock.  (Ordinarily, the group lock nests inside
	   the bufferevent locks.  If we are unable to lock any individual
	   bufferevent, it will find out later when it looks at its limit
	   and sees that its group is suspended.)
	*/
	LIST_FOREACH(bev, &g->members, rate_limiting->next_in_group) {
		if (EVLOCK_TRY_LOCK_(bev->lock)) {
			bufferevent_suspend_read_(&bev->bev,
			    BEV_SUSPEND_BW_GROUP);
			EVLOCK_UNLOCK(bev->lock, 0);
		}
	}
	return 0;
}

/** Stop writing on every bufferevent in <b>g</b> */
static int
bev_group_suspend_writing_(struct bufferevent_rate_limit_group *g)
{
	/* Needs group lock */
	struct bufferevent_private *bev;
	g->write_suspended = 1;
	g->pending_unsuspend_write = 0;
	LIST_FOREACH(bev, &g->members, rate_limiting->next_in_group) {
		if (EVLOCK_TRY_LOCK_(bev->lock)) {
			bufferevent_suspend_write_(&bev->bev,
			    BEV_SUSPEND_BW_GROUP);
			EVLOCK_UNLOCK(bev->lock, 0);
		}
	}
	return 0;
}

/** Timer callback invoked on a single bufferevent with one or more exhausted
    buckets when they are ready to refill. */
static void
bev_refill_callback_(evutil_socket_t fd, short what, void *arg)
{
	unsigned tick;
	struct timeval now;
	struct bufferevent_private *bev = arg;
	int again = 0;
	BEV_LOCK(&bev->bev);
	if (!bev->rate_limiting || !bev->rate_limiting->cfg) {
		BEV_UNLOCK(&bev->bev);
		return;
	}

	/* First, update the bucket */
	event_base_gettimeofday_cached(bev->bev.ev_base, &now);
	tick = ev_token_bucket_get_tick_(&now,
	    bev->rate_limiting->cfg);
	ev_token_bucket_update_(&bev->rate_limiting->limit,
	    bev->rate_limiting->cfg,
	    tick);

	/* Now unsuspend any read/write operations as appropriate. */
	if ((bev->read_suspended & BEV_SUSPEND_BW)) {
		if (bev->rate_limiting->limit.read_limit > 0)
			bufferevent_unsuspend_read_(&bev->bev, BEV_SUSPEND_BW);
		else
			again = 1;
	}
	if ((bev->write_suspended & BEV_SUSPEND_BW)) {
		if (bev->rate_limiting->limit.write_limit > 0)
			bufferevent_unsuspend_write_(&bev->bev, BEV_SUSPEND_BW);
		else
			again = 1;
	}
	if (again) {
		/* One or more of the buckets may need another refill if they
		   started negative.

		   XXXX if we need to be quiet for more ticks, we should
		   maybe figure out what timeout we really want.
		*/
		/* XXXX Handle event_add failure somehow */
		event_add(&bev->rate_limiting->refill_bucket_event,
		    &bev->rate_limiting->cfg->tick_timeout);
	}
	BEV_UNLOCK(&bev->bev);
}

/** Helper: grab a random element from a bufferevent group.
 *
 * Requires that we hold the lock on the group.
 */
static struct bufferevent_private *
bev_group_random_element_(struct bufferevent_rate_limit_group *group)
{
	int which;
	struct bufferevent_private *bev;

	/* requires group lock */

	if (!group->n_members)
		return NULL;

	EVUTIL_ASSERT(! LIST_EMPTY(&group->members));

	which = evutil_weakrand_range_(&group->weakrand_seed, group->n_members);

	bev = LIST_FIRST(&group->members);
	while (which--)
		bev = LIST_NEXT(bev, rate_limiting->next_in_group);

	return bev;
}

/** Iterate over the elements of a rate-limiting group 'g' with a random
    starting point, assigning each to the variable 'bev', and executing the
    block 'block'.

    We do this in a half-baked effort to get fairness among group members.
    XXX Round-robin or some kind of priority queue would be even more fair.
 */
#define FOREACH_RANDOM_ORDER(block)			\
	do {						\
		first = bev_group_random_element_(g);	\
		for (bev = first; bev != LIST_END(&g->members); \
		    bev = LIST_NEXT(bev, rate_limiting->next_in_group)) { \
			block ;					 \
		}						 \
		for (bev = LIST_FIRST(&g->members); bev && bev != first; \
		    bev = LIST_NEXT(bev, rate_limiting->next_in_group)) { \
			block ;						\
		}							\
	} while (0)

static void
bev_group_unsuspend_reading_(struct bufferevent_rate_limit_group *g)
{
	int again = 0;
	struct bufferevent_private *bev, *first;

	g->read_suspended = 0;
	FOREACH_RANDOM_ORDER({
		if (EVLOCK_TRY_LOCK_(bev->lock)) {
			bufferevent_unsuspend_read_(&bev->bev,
			    BEV_SUSPEND_BW_GROUP);
			EVLOCK_UNLOCK(bev->lock, 0);
		} else {
			again = 1;
		}
	});
	g->pending_unsuspend_read = again;
}

static void
bev_group_unsuspend_writing_(struct bufferevent_rate_limit_group *g)
{
	int again = 0;
	struct bufferevent_private *bev, *first;
	g->write_suspended = 0;

	FOREACH_RANDOM_ORDER({
		if (EVLOCK_TRY_LOCK_(bev->lock)) {
			bufferevent_unsuspend_write_(&bev->bev,
			    BEV_SUSPEND_BW_GROUP);
			EVLOCK_UNLOCK(bev->lock, 0);
		} else {
			again = 1;
		}
	});
	g->pending_unsuspend_write = again;
}

/** Callback invoked every tick to add more elements to the group bucket
    and unsuspend group members as needed.
 */
static void
bev_group_refill_callback_(evutil_socket_t fd, short what, void *arg)
{
	struct bufferevent_rate_limit_group *g = arg;
	unsigned tick;
	struct timeval now;

	event_base_gettimeofday_cached(event_get_base(&g->master_refill_event), &now);

	LOCK_GROUP(g);

	tick = ev_token_bucket_get_tick_(&now, &g->rate_limit_cfg);
	ev_token_bucket_update_(&g->rate_limit, &g->rate_limit_cfg, tick);

	if (g->pending_unsuspend_read ||
	    (g->read_suspended && (g->rate_limit.read_limit >= g->min_share))) {
		bev_group_unsuspend_reading_(g);
	}
	if (g->pending_unsuspend_write ||
	    (g->write_suspended && (g->rate_limit.write_limit >= g->min_share))){
		bev_group_unsuspend_writing_(g);
	}

	/* XXXX Rather than waiting to the next tick to unsuspend stuff
	 * with pending_unsuspend_write/read, we should do it on the
	 * next iteration of the mainloop.
	 */

	UNLOCK_GROUP(g);
}

int
bufferevent_set_rate_limit(struct bufferevent *bev,
    struct ev_token_bucket_cfg *cfg)
{
	struct bufferevent_private *bevp =
	    EVUTIL_UPCAST(bev, struct bufferevent_private, bev);
	int r = -1;
	struct bufferevent_rate_limit *rlim;
	struct timeval now;
	ev_uint32_t tick;
	int reinit = 0, suspended = 0;
	/* XXX reference-count cfg */

	BEV_LOCK(bev);

	if (cfg == NULL) {
		if (bevp->rate_limiting) {
			rlim = bevp->rate_limiting;
			rlim->cfg = NULL;
			bufferevent_unsuspend_read_(bev, BEV_SUSPEND_BW);
			bufferevent_unsuspend_write_(bev, BEV_SUSPEND_BW);
			if (event_initialized(&rlim->refill_bucket_event))
				event_del(&rlim->refill_bucket_event);
		}
		r = 0;
		goto done;
	}

	event_base_gettimeofday_cached(bev->ev_base, &now);
	tick = ev_token_bucket_get_tick_(&now, cfg);

	if (bevp->rate_limiting && bevp->rate_limiting->cfg == cfg) {
		/* no-op */
		r = 0;
		goto done;
	}
	if (bevp->rate_limiting == NULL) {
		rlim = mm_calloc(1, sizeof(struct bufferevent_rate_limit));
		if (!rlim)
			goto done;
		bevp->rate_limiting = rlim;
	} else {
		rlim = bevp->rate_limiting;
	}
	reinit = rlim->cfg != NULL;

	rlim->cfg = cfg;
	ev_token_bucket_init_(&rlim->limit, cfg, tick, reinit);

	if (reinit) {
		EVUTIL_ASSERT(event_initialized(&rlim->refill_bucket_event));
		event_del(&rlim->refill_bucket_event);
	}
	event_assign(&rlim->refill_bucket_event, bev->ev_base,
	    -1, EV_FINALIZE, bev_refill_callback_, bevp);

	if (rlim->limit.read_limit > 0) {
		bufferevent_unsuspend_read_(bev, BEV_SUSPEND_BW);
	} else {
		bufferevent_suspend_read_(bev, BEV_SUSPEND_BW);
		suspended=1;
	}
	if (rlim->limit.write_limit > 0) {
		bufferevent_unsuspend_write_(bev, BEV_SUSPEND_BW);
	} else {
		bufferevent_suspend_write_(bev, BEV_SUSPEND_BW);
		suspended = 1;
	}

	if (suspended)
		event_add(&rlim->refill_bucket_event, &cfg->tick_timeout);

	r = 0;

done:
	BEV_UNLOCK(bev);
	return r;
}

struct bufferevent_rate_limit_group *
bufferevent_rate_limit_group_new(struct event_base *base,
    const struct ev_token_bucket_cfg *cfg)
{
	struct bufferevent_rate_limit_group *g;
	struct timeval now;
	ev_uint32_t tick;

	event_base_gettimeofday_cached(base, &now);
	tick = ev_token_bucket_get_tick_(&now, cfg);

	g = mm_calloc(1, sizeof(struct bufferevent_rate_limit_group));
	if (!g)
		return NULL;
	memcpy(&g->rate_limit_cfg, cfg, sizeof(g->rate_limit_cfg));
	LIST_INIT(&g->members);

	ev_token_bucket_init_(&g->rate_limit, cfg, tick, 0);

	event_assign(&g->master_refill_event, base, -1, EV_PERSIST|EV_FINALIZE,
	    bev_group_refill_callback_, g);
	/*XXXX handle event_add failure */
	event_add(&g->master_refill_event, &cfg->tick_timeout);

	EVTHREAD_ALLOC_LOCK(g->lock, EVTHREAD_LOCKTYPE_RECURSIVE);

	bufferevent_rate_limit_group_set_min_share(g, 64);

	evutil_weakrand_seed_(&g->weakrand_seed,
	    (ev_uint32_t) ((now.tv_sec + now.tv_usec) + (ev_intptr_t)g));

	return g;
}

int
bufferevent_rate_limit_group_set_cfg(
	struct bufferevent_rate_limit_group *g,
	const struct ev_token_bucket_cfg *cfg)
{
	int same_tick;
	if (!g || !cfg)
		return -1;

	LOCK_GROUP(g);
	same_tick = evutil_timercmp(
		&g->rate_limit_cfg.tick_timeout, &cfg->tick_timeout, ==);
	memcpy(&g->rate_limit_cfg, cfg, sizeof(g->rate_limit_cfg));

	if (g->rate_limit.read_limit > (ev_ssize_t)cfg->read_maximum)
		g->rate_limit.read_limit = cfg->read_maximum;
	if (g->rate_limit.write_limit > (ev_ssize_t)cfg->write_maximum)
		g->rate_limit.write_limit = cfg->write_maximum;

	if (!same_tick) {
		/* This can cause a hiccup in the schedule */
		event_add(&g->master_refill_event, &cfg->tick_timeout);
	}

	/* The new limits might force us to adjust min_share differently. */
	bufferevent_rate_limit_group_set_min_share(g, g->configured_min_share);

	UNLOCK_GROUP(g);
	return 0;
}

int
bufferevent_rate_limit_group_set_min_share(
	struct bufferevent_rate_limit_group *g,
	size_t share)
{
	if (share > EV_SSIZE_MAX)
		return -1;

	g->configured_min_share = share;

	/* Can't set share to less than the one-tick maximum.  IOW, at steady
	 * state, at least one connection can go per tick. */
	if (share > g->rate_limit_cfg.read_rate)
		share = g->rate_limit_cfg.read_rate;
	if (share > g->rate_limit_cfg.write_rate)
		share = g->rate_limit_cfg.write_rate;

	g->min_share = share;
	return 0;
}

void
bufferevent_rate_limit_group_free(struct bufferevent_rate_limit_group *g)
{
	LOCK_GROUP(g);
	EVUTIL_ASSERT(0 == g->n_members);
	event_del(&g->master_refill_event);
	UNLOCK_GROUP(g);
	EVTHREAD_FREE_LOCK(g->lock, EVTHREAD_LOCKTYPE_RECURSIVE);
	mm_free(g);
}

int
bufferevent_add_to_rate_limit_group(struct bufferevent *bev,
    struct bufferevent_rate_limit_group *g)
{
	int wsuspend, rsuspend;
	struct bufferevent_private *bevp =
	    EVUTIL_UPCAST(bev, struct bufferevent_private, bev);
	BEV_LOCK(bev);

	if (!bevp->rate_limiting) {
		struct bufferevent_rate_limit *rlim;
		rlim = mm_calloc(1, sizeof(struct bufferevent_rate_limit));
		if (!rlim) {
			BEV_UNLOCK(bev);
			return -1;
		}
		event_assign(&rlim->refill_bucket_event, bev->ev_base,
		    -1, EV_FINALIZE, bev_refill_callback_, bevp);
		bevp->rate_limiting = rlim;
	}

	if (bevp->rate_limiting->group == g) {
		BEV_UNLOCK(bev);
		return 0;
	}
	if (bevp->rate_limiting->group)
		bufferevent_remove_from_rate_limit_group(bev);

	LOCK_GROUP(g);
	bevp->rate_limiting->group = g;
	++g->n_members;
	LIST_INSERT_HEAD(&g->members, bevp, rate_limiting->next_in_group);

	rsuspend = g->read_suspended;
	wsuspend = g->write_suspended;

	UNLOCK_GROUP(g);

	if (rsuspend)
		bufferevent_suspend_read_(bev, BEV_SUSPEND_BW_GROUP);
	if (wsuspend)
		bufferevent_suspend_write_(bev, BEV_SUSPEND_BW_GROUP);

	BEV_UNLOCK(bev);
	return 0;
}

int
bufferevent_remove_from_rate_limit_group(struct bufferevent *bev)
{
	return bufferevent_remove_from_rate_limit_group_internal_(bev, 1);
}

int
bufferevent_remove_from_rate_limit_group_internal_(struct bufferevent *bev,
    int unsuspend)
{
	struct bufferevent_private *bevp =
	    EVUTIL_UPCAST(bev, struct bufferevent_private, bev);
	BEV_LOCK(bev);
	if (bevp->rate_limiting && bevp->rate_limiting->group) {
		struct bufferevent_rate_limit_group *g =
		    bevp->rate_limiting->group;
		LOCK_GROUP(g);
		bevp->rate_limiting->group = NULL;
		--g->n_members;
		LIST_REMOVE(bevp, rate_limiting->next_in_group);
		UNLOCK_GROUP(g);
	}
	if (unsuspend) {
		bufferevent_unsuspend_read_(bev, BEV_SUSPEND_BW_GROUP);
		bufferevent_unsuspend_write_(bev, BEV_SUSPEND_BW_GROUP);
	}
	BEV_UNLOCK(bev);
	return 0;
}

/* ===
 * API functions to expose rate limits.
 *
 * Don't use these from inside Libevent; they're meant to be for use by
 * the program.
 * === */

/* Mostly you don't want to use this function from inside libevent;
 * bufferevent_get_read_max_() is more likely what you want*/
ev_ssize_t
bufferevent_get_read_limit(struct bufferevent *bev)
{
	ev_ssize_t r;
	struct bufferevent_private *bevp;
	BEV_LOCK(bev);
	bevp = BEV_UPCAST(bev);
	if (bevp->rate_limiting && bevp->rate_limiting->cfg) {
		bufferevent_update_buckets(bevp);
		r = bevp->rate_limiting->limit.read_limit;
	} else {
		r = EV_SSIZE_MAX;
	}
	BEV_UNLOCK(bev);
	return r;
}

/* Mostly you don't want to use this function from inside libevent;
 * bufferevent_get_write_max_() is more likely what you want*/
ev_ssize_t
bufferevent_get_write_limit(struct bufferevent *bev)
{
	ev_ssize_t r;
	struct bufferevent_private *bevp;
	BEV_LOCK(bev);
	bevp = BEV_UPCAST(bev);
	if (bevp->rate_limiting && bevp->rate_limiting->cfg) {
		bufferevent_update_buckets(bevp);
		r = bevp->rate_limiting->limit.write_limit;
	} else {
		r = EV_SSIZE_MAX;
	}
	BEV_UNLOCK(bev);
	return r;
}

int
bufferevent_set_max_single_read(struct bufferevent *bev, size_t size)
{
	struct bufferevent_private *bevp;
	BEV_LOCK(bev);
	bevp = BEV_UPCAST(bev);
	if (size == 0 || size > EV_SSIZE_MAX)
		bevp->max_single_read = MAX_SINGLE_READ_DEFAULT;
	else
		bevp->max_single_read = size;
	BEV_UNLOCK(bev);
	return 0;
}

int
bufferevent_set_max_single_write(struct bufferevent *bev, size_t size)
{
	struct bufferevent_private *bevp;
	BEV_LOCK(bev);
	bevp = BEV_UPCAST(bev);
	if (size == 0 || size > EV_SSIZE_MAX)
		bevp->max_single_write = MAX_SINGLE_WRITE_DEFAULT;
	else
		bevp->max_single_write = size;
	BEV_UNLOCK(bev);
	return 0;
}

ev_ssize_t
bufferevent_get_max_single_read(struct bufferevent *bev)
{
	ev_ssize_t r;

	BEV_LOCK(bev);
	r = BEV_UPCAST(bev)->max_single_read;
	BEV_UNLOCK(bev);
	return r;
}

ev_ssize_t
bufferevent_get_max_single_write(struct bufferevent *bev)
{
	ev_ssize_t r;

	BEV_LOCK(bev);
	r = BEV_UPCAST(bev)->max_single_write;
	BEV_UNLOCK(bev);
	return r;
}

ev_ssize_t
bufferevent_get_max_to_read(struct bufferevent *bev)
{
	ev_ssize_t r;
	BEV_LOCK(bev);
	r = bufferevent_get_read_max_(BEV_UPCAST(bev));
	BEV_UNLOCK(bev);
	return r;
}

ev_ssize_t
bufferevent_get_max_to_write(struct bufferevent *bev)
{
	ev_ssize_t r;
	BEV_LOCK(bev);
	r = bufferevent_get_write_max_(BEV_UPCAST(bev));
	BEV_UNLOCK(bev);
	return r;
}

const struct ev_token_bucket_cfg *
bufferevent_get_token_bucket_cfg(const struct bufferevent *bev) {
	struct bufferevent_private *bufev_private = BEV_UPCAST(bev);
	struct ev_token_bucket_cfg *cfg;

	BEV_LOCK(bev);

	if (bufev_private->rate_limiting) {
		cfg = bufev_private->rate_limiting->cfg;
	} else {
		cfg = NULL;
	}

	BEV_UNLOCK(bev);

	return cfg;
}

/* Mostly you don't want to use this function from inside libevent;
 * bufferevent_get_read_max_() is more likely what you want*/
ev_ssize_t
bufferevent_rate_limit_group_get_read_limit(
	struct bufferevent_rate_limit_group *grp)
{
	ev_ssize_t r;
	LOCK_GROUP(grp);
	r = grp->rate_limit.read_limit;
	UNLOCK_GROUP(grp);
	return r;
}

/* Mostly you don't want to use this function from inside libevent;
 * bufferevent_get_write_max_() is more likely what you want. */
ev_ssize_t
bufferevent_rate_limit_group_get_write_limit(
	struct bufferevent_rate_limit_group *grp)
{
	ev_ssize_t r;
	LOCK_GROUP(grp);
	r = grp->rate_limit.write_limit;
	UNLOCK_GROUP(grp);
	return r;
}

int
bufferevent_decrement_read_limit(struct bufferevent *bev, ev_ssize_t decr)
{
	int r = 0;
	ev_ssize_t old_limit, new_limit;
	struct bufferevent_private *bevp;
	BEV_LOCK(bev);
	bevp = BEV_UPCAST(bev);
	EVUTIL_ASSERT(bevp->rate_limiting && bevp->rate_limiting->cfg);
	old_limit = bevp->rate_limiting->limit.read_limit;

	new_limit = (bevp->rate_limiting->limit.read_limit -= decr);
	if (old_limit > 0 && new_limit <= 0) {
		bufferevent_suspend_read_(bev, BEV_SUSPEND_BW);
		if (event_add(&bevp->rate_limiting->refill_bucket_event,
			&bevp->rate_limiting->cfg->tick_timeout) < 0)
			r = -1;
	} else if (old_limit <= 0 && new_limit > 0) {
		if (!(bevp->write_suspended & BEV_SUSPEND_BW))
			event_del(&bevp->rate_limiting->refill_bucket_event);
		bufferevent_unsuspend_read_(bev, BEV_SUSPEND_BW);
	}

	BEV_UNLOCK(bev);
	return r;
}

int
bufferevent_decrement_write_limit(struct bufferevent *bev, ev_ssize_t decr)
{
	/* XXXX this is mostly copy-and-paste from
	 * bufferevent_decrement_read_limit */
	int r = 0;
	ev_ssize_t old_limit, new_limit;
	struct bufferevent_private *bevp;
	BEV_LOCK(bev);
	bevp = BEV_UPCAST(bev);
	EVUTIL_ASSERT(bevp->rate_limiting && bevp->rate_limiting->cfg);
	old_limit = bevp->rate_limiting->limit.write_limit;

	new_limit = (bevp->rate_limiting->limit.write_limit -= decr);
	if (old_limit > 0 && new_limit <= 0) {
		bufferevent_suspend_write_(bev, BEV_SUSPEND_BW);
		if (event_add(&bevp->rate_limiting->refill_bucket_event,
			&bevp->rate_limiting->cfg->tick_timeout) < 0)
			r = -1;
	} else if (old_limit <= 0 && new_limit > 0) {
		if (!(bevp->read_suspended & BEV_SUSPEND_BW))
			event_del(&bevp->rate_limiting->refill_bucket_event);
		bufferevent_unsuspend_write_(bev, BEV_SUSPEND_BW);
	}

	BEV_UNLOCK(bev);
	return r;
}

int
bufferevent_rate_limit_group_decrement_read(
	struct bufferevent_rate_limit_group *grp, ev_ssize_t decr)
{
	int r = 0;
	ev_ssize_t old_limit, new_limit;
	LOCK_GROUP(grp);
	old_limit = grp->rate_limit.read_limit;
	new_limit = (grp->rate_limit.read_limit -= decr);

	if (old_limit > 0 && new_limit <= 0) {
		bev_group_suspend_reading_(grp);
	} else if (old_limit <= 0 && new_limit > 0) {
		bev_group_unsuspend_reading_(grp);
	}

	UNLOCK_GROUP(grp);
	return r;
}

int
bufferevent_rate_limit_group_decrement_write(
	struct bufferevent_rate_limit_group *grp, ev_ssize_t decr)
{
	int r = 0;
	ev_ssize_t old_limit, new_limit;
	LOCK_GROUP(grp);
	old_limit = grp->rate_limit.write_limit;
	new_limit = (grp->rate_limit.write_limit -= decr);

	if (old_limit > 0 && new_limit <= 0) {
		bev_group_suspend_writing_(grp);
	} else if (old_limit <= 0 && new_limit > 0) {
		bev_group_unsuspend_writing_(grp);
	}

	UNLOCK_GROUP(grp);
	return r;
}

void
bufferevent_rate_limit_group_get_totals(struct bufferevent_rate_limit_group *grp,
    ev_uint64_t *total_read_out, ev_uint64_t *total_written_out)
{
	EVUTIL_ASSERT(grp != NULL);
	if (total_read_out)
		*total_read_out = grp->total_read;
	if (total_written_out)
		*total_written_out = grp->total_written;
}

void
bufferevent_rate_limit_group_reset_totals(struct bufferevent_rate_limit_group *grp)
{
	grp->total_read = grp->total_written = 0;
}

int
bufferevent_ratelim_init_(struct bufferevent_private *bev)
{
	bev->rate_limiting = NULL;
	bev->max_single_read = MAX_SINGLE_READ_DEFAULT;
	bev->max_single_write = MAX_SINGLE_WRITE_DEFAULT;

	return 0;
}
