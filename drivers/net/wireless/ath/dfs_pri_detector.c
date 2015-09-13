/*
 * Copyright (c) 2012 Neratec Solutions AG
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <linux/slab.h>
#include <linux/spinlock.h>

#include "ath.h"
#include "dfs_pattern_detector.h"
#include "dfs_pri_detector.h"

struct ath_dfs_pool_stats global_dfs_pool_stats = {};

#define DFS_POOL_STAT_INC(c) (global_dfs_pool_stats.c++)
#define DFS_POOL_STAT_DEC(c) (global_dfs_pool_stats.c--)

/**
 * struct pulse_elem - elements in pulse queue
 * @ts: time stamp in usecs
 */
struct pulse_elem {
	struct list_head head;
	u64 ts;
};

/**
 * pde_get_multiple() - get number of multiples considering a given tolerance
 * @return factor if abs(val - factor*fraction) <= tolerance, 0 otherwise
 */
static u32 pde_get_multiple(u32 val, u32 fraction, u32 tolerance)
{
	u32 remainder;
	u32 factor;
	u32 delta;

	if (fraction == 0)
		return 0;

	delta = (val < fraction) ? (fraction - val) : (val - fraction);

	if (delta <= tolerance)
		/* val and fraction are within tolerance */
		return 1;

	factor = val / fraction;
	remainder = val % fraction;
	if (remainder > tolerance) {
		/* no exact match */
		if ((fraction - remainder) <= tolerance)
			/* remainder is within tolerance */
			factor++;
		else
			factor = 0;
	}
	return factor;
}

/**
 * DOC: Singleton Pulse and Sequence Pools
 *
 * Instances of pri_sequence and pulse_elem are kept in singleton pools to
 * reduce the number of dynamic allocations. They are shared between all
 * instances and grow up to the peak number of simultaneously used objects.
 *
 * Memory is freed after all references to the pools are released.
 */
static u32 singleton_pool_references;
static LIST_HEAD(pulse_pool);
static LIST_HEAD(pseq_pool);
static DEFINE_SPINLOCK(pool_lock);

static void pool_register_ref(void)
{
	spin_lock_bh(&pool_lock);
	singleton_pool_references++;
	DFS_POOL_STAT_INC(pool_reference);
	spin_unlock_bh(&pool_lock);
}

static void pool_deregister_ref(void)
{
	spin_lock_bh(&pool_lock);
	singleton_pool_references--;
	DFS_POOL_STAT_DEC(pool_reference);
	if (singleton_pool_references == 0) {
		/* free singleton pools with no references left */
		struct pri_sequence *ps, *ps0;
		struct pulse_elem *p, *p0;

		list_for_each_entry_safe(p, p0, &pulse_pool, head) {
			list_del(&p->head);
			DFS_POOL_STAT_DEC(pulse_allocated);
			kfree(p);
		}
		list_for_each_entry_safe(ps, ps0, &pseq_pool, head) {
			list_del(&ps->head);
			DFS_POOL_STAT_DEC(pseq_allocated);
			kfree(ps);
		}
	}
	spin_unlock_bh(&pool_lock);
}

static void pool_put_pulse_elem(struct pulse_elem *pe)
{
	spin_lock_bh(&pool_lock);
	list_add(&pe->head, &pulse_pool);
	DFS_POOL_STAT_DEC(pulse_used);
	spin_unlock_bh(&pool_lock);
}

static void pool_put_pseq_elem(struct pri_sequence *pse)
{
	spin_lock_bh(&pool_lock);
	list_add(&pse->head, &pseq_pool);
	DFS_POOL_STAT_DEC(pseq_used);
	spin_unlock_bh(&pool_lock);
}

static struct pri_sequence *pool_get_pseq_elem(void)
{
	struct pri_sequence *pse = NULL;
	spin_lock_bh(&pool_lock);
	if (!list_empty(&pseq_pool)) {
		pse = list_first_entry(&pseq_pool, struct pri_sequence, head);
		list_del(&pse->head);
		DFS_POOL_STAT_INC(pseq_used);
	}
	spin_unlock_bh(&pool_lock);
	return pse;
}

static struct pulse_elem *pool_get_pulse_elem(void)
{
	struct pulse_elem *pe = NULL;
	spin_lock_bh(&pool_lock);
	if (!list_empty(&pulse_pool)) {
		pe = list_first_entry(&pulse_pool, struct pulse_elem, head);
		list_del(&pe->head);
		DFS_POOL_STAT_INC(pulse_used);
	}
	spin_unlock_bh(&pool_lock);
	return pe;
}

static struct pulse_elem *pulse_queue_get_tail(struct pri_detector *pde)
{
	struct list_head *l = &pde->pulses;
	if (list_empty(l))
		return NULL;
	return list_entry(l->prev, struct pulse_elem, head);
}

static bool pulse_queue_dequeue(struct pri_detector *pde)
{
	struct pulse_elem *p = pulse_queue_get_tail(pde);
	if (p != NULL) {
		list_del_init(&p->head);
		pde->count--;
		/* give it back to pool */
		pool_put_pulse_elem(p);
	}
	return (pde->count > 0);
}

/* remove pulses older than window */
static void pulse_queue_check_window(struct pri_detector *pde)
{
	u64 min_valid_ts;
	struct pulse_elem *p;

	/* there is no delta time with less than 2 pulses */
	if (pde->count < 2)
		return;

	if (pde->last_ts <= pde->window_size)
		return;

	min_valid_ts = pde->last_ts - pde->window_size;
	while ((p = pulse_queue_get_tail(pde)) != NULL) {
		if (p->ts >= min_valid_ts)
			return;
		pulse_queue_dequeue(pde);
	}
}

static bool pulse_queue_enqueue(struct pri_detector *pde, u64 ts)
{
	struct pulse_elem *p = pool_get_pulse_elem();
	if (p == NULL) {
		p = kmalloc(sizeof(*p), GFP_ATOMIC);
		if (p == NULL) {
			DFS_POOL_STAT_INC(pulse_alloc_error);
			return false;
		}
		DFS_POOL_STAT_INC(pulse_allocated);
		DFS_POOL_STAT_INC(pulse_used);
	}
	INIT_LIST_HEAD(&p->head);
	p->ts = ts;
	list_add(&p->head, &pde->pulses);
	pde->count++;
	pde->last_ts = ts;
	pulse_queue_check_window(pde);
	if (pde->count >= pde->max_count)
		pulse_queue_dequeue(pde);
	return true;
}

static bool pseq_handler_create_sequences(struct pri_detector *pde,
					  u64 ts, u32 min_count)
{
	struct pulse_elem *p;
	list_for_each_entry(p, &pde->pulses, head) {
		struct pri_sequence ps, *new_ps;
		struct pulse_elem *p2;
		u32 tmp_false_count;
		u64 min_valid_ts;
		u32 delta_ts = ts - p->ts;

		if (delta_ts < pde->rs->pri_min)
			/* ignore too small pri */
			continue;

		if (delta_ts > pde->rs->pri_max)
			/* stop on too large pri (sorted list) */
			break;

		/* build a new sequence with new potential pri */
		ps.count = 2;
		ps.count_falses = 0;
		ps.first_ts = p->ts;
		ps.last_ts = ts;
		ps.pri = ts - p->ts;
		ps.dur = ps.pri * (pde->rs->ppb - 1)
				+ 2 * pde->rs->max_pri_tolerance;

		p2 = p;
		tmp_false_count = 0;
		min_valid_ts = ts - ps.dur;
		/* check which past pulses are candidates for new sequence */
		list_for_each_entry_continue(p2, &pde->pulses, head) {
			u32 factor;
			if (p2->ts < min_valid_ts)
				/* stop on crossing window border */
				break;
			/* check if pulse match (multi)PRI */
			factor = pde_get_multiple(ps.last_ts - p2->ts, ps.pri,
						  pde->rs->max_pri_tolerance);
			if (factor > 0) {
				ps.count++;
				ps.first_ts = p2->ts;
				/*
				 * on match, add the intermediate falses
				 * and reset counter
				 */
				ps.count_falses += tmp_false_count;
				tmp_false_count = 0;
			} else {
				/* this is a potential false one */
				tmp_false_count++;
			}
		}
		if (ps.count <= min_count)
			/* did not reach minimum count, drop sequence */
			continue;

		/* this is a valid one, add it */
		ps.deadline_ts = ps.first_ts + ps.dur;
		new_ps = pool_get_pseq_elem();
		if (new_ps == NULL) {
			new_ps = kmalloc(sizeof(*new_ps), GFP_ATOMIC);
			if (new_ps == NULL) {
				DFS_POOL_STAT_INC(pseq_alloc_error);
				return false;
			}
			DFS_POOL_STAT_INC(pseq_allocated);
			DFS_POOL_STAT_INC(pseq_used);
		}
		memcpy(new_ps, &ps, sizeof(ps));
		INIT_LIST_HEAD(&new_ps->head);
		list_add(&new_ps->head, &pde->sequences);
	}
	return true;
}

/* check new ts and add to all matching existing sequences */
static u32
pseq_handler_add_to_existing_seqs(struct pri_detector *pde, u64 ts)
{
	u32 max_count = 0;
	struct pri_sequence *ps, *ps2;
	list_for_each_entry_safe(ps, ps2, &pde->sequences, head) {
		u32 delta_ts;
		u32 factor;

		/* first ensure that sequence is within window */
		if (ts > ps->deadline_ts) {
			list_del_init(&ps->head);
			pool_put_pseq_elem(ps);
			continue;
		}

		delta_ts = ts - ps->last_ts;
		factor = pde_get_multiple(delta_ts, ps->pri,
					  pde->rs->max_pri_tolerance);
		if (factor > 0) {
			ps->last_ts = ts;
			ps->count++;

			if (max_count < ps->count)
				max_count = ps->count;
		} else {
			ps->count_falses++;
		}
	}
	return max_count;
}

static struct pri_sequence *
pseq_handler_check_detection(struct pri_detector *pde)
{
	struct pri_sequence *ps;

	if (list_empty(&pde->sequences))
		return NULL;

	list_for_each_entry(ps, &pde->sequences, head) {
		/*
		 * we assume to have enough matching confidence if we
		 * 1) have enough pulses
		 * 2) have more matching than false pulses
		 */
		if ((ps->count >= pde->rs->ppb_thresh) &&
		    (ps->count * pde->rs->num_pri >= ps->count_falses))
			return ps;
	}
	return NULL;
}


/* free pulse queue and sequences list and give objects back to pools */
static void pri_detector_reset(struct pri_detector *pde, u64 ts)
{
	struct pri_sequence *ps, *ps0;
	struct pulse_elem *p, *p0;
	list_for_each_entry_safe(ps, ps0, &pde->sequences, head) {
		list_del_init(&ps->head);
		pool_put_pseq_elem(ps);
	}
	list_for_each_entry_safe(p, p0, &pde->pulses, head) {
		list_del_init(&p->head);
		pool_put_pulse_elem(p);
	}
	pde->count = 0;
	pde->last_ts = ts;
}

static void pri_detector_exit(struct pri_detector *de)
{
	pri_detector_reset(de, 0);
	pool_deregister_ref();
	kfree(de);
}

static struct pri_sequence *pri_detector_add_pulse(struct pri_detector *de,
						   struct pulse_event *event)
{
	u32 max_updated_seq;
	struct pri_sequence *ps;
	u64 ts = event->ts;
	const struct radar_detector_specs *rs = de->rs;

	/* ignore pulses not within width range */
	if ((rs->width_min > event->width) || (rs->width_max < event->width))
		return NULL;

	if ((ts - de->last_ts) < rs->max_pri_tolerance)
		/* if delta to last pulse is too short, don't use this pulse */
		return NULL;
	/* radar detector spec needs chirp, but not detected */
	if (rs->chirp && rs->chirp != event->chirp)
		return NULL;

	de->last_ts = ts;

	max_updated_seq = pseq_handler_add_to_existing_seqs(de, ts);

	if (!pseq_handler_create_sequences(de, ts, max_updated_seq)) {
		pri_detector_reset(de, ts);
		return NULL;
	}

	ps = pseq_handler_check_detection(de);

	if (ps == NULL)
		pulse_queue_enqueue(de, ts);

	return ps;
}

struct pri_detector *pri_detector_init(const struct radar_detector_specs *rs)
{
	struct pri_detector *de;

	de = kzalloc(sizeof(*de), GFP_ATOMIC);
	if (de == NULL)
		return NULL;
	de->exit = pri_detector_exit;
	de->add_pulse = pri_detector_add_pulse;
	de->reset = pri_detector_reset;

	INIT_LIST_HEAD(&de->sequences);
	INIT_LIST_HEAD(&de->pulses);
	de->window_size = rs->pri_max * rs->ppb * rs->num_pri;
	de->max_count = rs->ppb * 2;
	de->rs = rs;

	pool_register_ref();
	return de;
}
