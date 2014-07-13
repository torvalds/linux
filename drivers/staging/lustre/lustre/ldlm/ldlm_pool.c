/*
 * GPL HEADER START
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License version 2 for more details (a copy is included
 * in the LICENSE file that accompanied this code).
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; If not, see
 * http://www.sun.com/software/products/lustre/docs/GPLv2.pdf
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa Clara,
 * CA 95054 USA or visit www.sun.com if you need additional information or
 * have any questions.
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2007, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2010, 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lustre/ldlm/ldlm_pool.c
 *
 * Author: Yury Umanets <umka@clusterfs.com>
 */

/*
 * Idea of this code is rather simple. Each second, for each server namespace
 * we have SLV - server lock volume which is calculated on current number of
 * granted locks, grant speed for past period, etc - that is, locking load.
 * This SLV number may be thought as a flow definition for simplicity. It is
 * sent to clients with each occasion to let them know what is current load
 * situation on the server. By default, at the beginning, SLV on server is
 * set max value which is calculated as the following: allow to one client
 * have all locks of limit ->pl_limit for 10h.
 *
 * Next, on clients, number of cached locks is not limited artificially in any
 * way as it was before. Instead, client calculates CLV, that is, client lock
 * volume for each lock and compares it with last SLV from the server. CLV is
 * calculated as the number of locks in LRU * lock live time in seconds. If
 * CLV > SLV - lock is canceled.
 *
 * Client has LVF, that is, lock volume factor which regulates how much sensitive
 * client should be about last SLV from server. The higher LVF is the more locks
 * will be canceled on client. Default value for it is 1. Setting LVF to 2 means
 * that client will cancel locks 2 times faster.
 *
 * Locks on a client will be canceled more intensively in these cases:
 * (1) if SLV is smaller, that is, load is higher on the server;
 * (2) client has a lot of locks (the more locks are held by client, the bigger
 *     chances that some of them should be canceled);
 * (3) client has old locks (taken some time ago);
 *
 * Thus, according to flow paradigm that we use for better understanding SLV,
 * CLV is the volume of particle in flow described by SLV. According to this,
 * if flow is getting thinner, more and more particles become outside of it and
 * as particles are locks, they should be canceled.
 *
 * General idea of this belongs to Vitaly Fertman (vitaly@clusterfs.com). Andreas
 * Dilger (adilger@clusterfs.com) proposed few nice ideas like using LVF and many
 * cleanups. Flow definition to allow more easy understanding of the logic belongs
 * to Nikita Danilov (nikita@clusterfs.com) as well as many cleanups and fixes.
 * And design and implementation are done by Yury Umanets (umka@clusterfs.com).
 *
 * Glossary for terms used:
 *
 * pl_limit - Number of allowed locks in pool. Applies to server and client
 * side (tunable);
 *
 * pl_granted - Number of granted locks (calculated);
 * pl_grant_rate - Number of granted locks for last T (calculated);
 * pl_cancel_rate - Number of canceled locks for last T (calculated);
 * pl_grant_speed - Grant speed (GR - CR) for last T (calculated);
 * pl_grant_plan - Planned number of granted locks for next T (calculated);
 * pl_server_lock_volume - Current server lock volume (calculated);
 *
 * As it may be seen from list above, we have few possible tunables which may
 * affect behavior much. They all may be modified via proc. However, they also
 * give a possibility for constructing few pre-defined behavior policies. If
 * none of predefines is suitable for a working pattern being used, new one may
 * be "constructed" via proc tunables.
 */

#define DEBUG_SUBSYSTEM S_LDLM

#include "../include/lustre_dlm.h"
#include "../include/cl_object.h"
#include "../include/obd_class.h"
#include "../include/obd_support.h"
#include "ldlm_internal.h"


/*
 * 50 ldlm locks for 1MB of RAM.
 */
#define LDLM_POOL_HOST_L ((NUM_CACHEPAGES >> (20 - PAGE_CACHE_SHIFT)) * 50)

/*
 * Maximal possible grant step plan in %.
 */
#define LDLM_POOL_MAX_GSP (30)

/*
 * Minimal possible grant step plan in %.
 */
#define LDLM_POOL_MIN_GSP (1)

/*
 * This controls the speed of reaching LDLM_POOL_MAX_GSP
 * with increasing thread period.
 */
#define LDLM_POOL_GSP_STEP_SHIFT (2)

/*
 * LDLM_POOL_GSP% of all locks is default GP.
 */
#define LDLM_POOL_GP(L)   (((L) * LDLM_POOL_MAX_GSP) / 100)

/*
 * Max age for locks on clients.
 */
#define LDLM_POOL_MAX_AGE (36000)

/*
 * The granularity of SLV calculation.
 */
#define LDLM_POOL_SLV_SHIFT (10)

extern struct proc_dir_entry *ldlm_ns_proc_dir;

static inline __u64 dru(__u64 val, __u32 shift, int round_up)
{
	return (val + (round_up ? (1 << shift) - 1 : 0)) >> shift;
}

static inline __u64 ldlm_pool_slv_max(__u32 L)
{
	/*
	 * Allow to have all locks for 1 client for 10 hrs.
	 * Formula is the following: limit * 10h / 1 client.
	 */
	__u64 lim = (__u64)L *  LDLM_POOL_MAX_AGE / 1;
	return lim;
}

static inline __u64 ldlm_pool_slv_min(__u32 L)
{
	return 1;
}

enum {
	LDLM_POOL_FIRST_STAT = 0,
	LDLM_POOL_GRANTED_STAT = LDLM_POOL_FIRST_STAT,
	LDLM_POOL_GRANT_STAT,
	LDLM_POOL_CANCEL_STAT,
	LDLM_POOL_GRANT_RATE_STAT,
	LDLM_POOL_CANCEL_RATE_STAT,
	LDLM_POOL_GRANT_PLAN_STAT,
	LDLM_POOL_SLV_STAT,
	LDLM_POOL_SHRINK_REQTD_STAT,
	LDLM_POOL_SHRINK_FREED_STAT,
	LDLM_POOL_RECALC_STAT,
	LDLM_POOL_TIMING_STAT,
	LDLM_POOL_LAST_STAT
};

static inline struct ldlm_namespace *ldlm_pl2ns(struct ldlm_pool *pl)
{
	return container_of(pl, struct ldlm_namespace, ns_pool);
}

/**
 * Calculates suggested grant_step in % of available locks for passed
 * \a period. This is later used in grant_plan calculations.
 */
static inline int ldlm_pool_t2gsp(unsigned int t)
{
	/*
	 * This yields 1% grant step for anything below LDLM_POOL_GSP_STEP
	 * and up to 30% for anything higher than LDLM_POOL_GSP_STEP.
	 *
	 * How this will affect execution is the following:
	 *
	 * - for thread period 1s we will have grant_step 1% which good from
	 * pov of taking some load off from server and push it out to clients.
	 * This is like that because 1% for grant_step means that server will
	 * not allow clients to get lots of locks in short period of time and
	 * keep all old locks in their caches. Clients will always have to
	 * get some locks back if they want to take some new;
	 *
	 * - for thread period 10s (which is default) we will have 23% which
	 * means that clients will have enough of room to take some new locks
	 * without getting some back. All locks from this 23% which were not
	 * taken by clients in current period will contribute in SLV growing.
	 * SLV growing means more locks cached on clients until limit or grant
	 * plan is reached.
	 */
	return LDLM_POOL_MAX_GSP -
		((LDLM_POOL_MAX_GSP - LDLM_POOL_MIN_GSP) >>
		 (t >> LDLM_POOL_GSP_STEP_SHIFT));
}

/**
 * Recalculates next grant limit on passed \a pl.
 *
 * \pre ->pl_lock is locked.
 */
static void ldlm_pool_recalc_grant_plan(struct ldlm_pool *pl)
{
	int granted, grant_step, limit;

	limit = ldlm_pool_get_limit(pl);
	granted = atomic_read(&pl->pl_granted);

	grant_step = ldlm_pool_t2gsp(pl->pl_recalc_period);
	grant_step = ((limit - granted) * grant_step) / 100;
	pl->pl_grant_plan = granted + grant_step;
	limit = (limit * 5) >> 2;
	if (pl->pl_grant_plan > limit)
		pl->pl_grant_plan = limit;
}

/**
 * Recalculates next SLV on passed \a pl.
 *
 * \pre ->pl_lock is locked.
 */
static void ldlm_pool_recalc_slv(struct ldlm_pool *pl)
{
	int granted;
	int grant_plan;
	int round_up;
	__u64 slv;
	__u64 slv_factor;
	__u64 grant_usage;
	__u32 limit;

	slv = pl->pl_server_lock_volume;
	grant_plan = pl->pl_grant_plan;
	limit = ldlm_pool_get_limit(pl);
	granted = atomic_read(&pl->pl_granted);
	round_up = granted < limit;

	grant_usage = max_t(int, limit - (granted - grant_plan), 1);

	/*
	 * Find out SLV change factor which is the ratio of grant usage
	 * from limit. SLV changes as fast as the ratio of grant plan
	 * consumption. The more locks from grant plan are not consumed
	 * by clients in last interval (idle time), the faster grows
	 * SLV. And the opposite, the more grant plan is over-consumed
	 * (load time) the faster drops SLV.
	 */
	slv_factor = (grant_usage << LDLM_POOL_SLV_SHIFT);
	do_div(slv_factor, limit);
	slv = slv * slv_factor;
	slv = dru(slv, LDLM_POOL_SLV_SHIFT, round_up);

	if (slv > ldlm_pool_slv_max(limit)) {
		slv = ldlm_pool_slv_max(limit);
	} else if (slv < ldlm_pool_slv_min(limit)) {
		slv = ldlm_pool_slv_min(limit);
	}

	pl->pl_server_lock_volume = slv;
}

/**
 * Recalculates next stats on passed \a pl.
 *
 * \pre ->pl_lock is locked.
 */
static void ldlm_pool_recalc_stats(struct ldlm_pool *pl)
{
	int grant_plan = pl->pl_grant_plan;
	__u64 slv = pl->pl_server_lock_volume;
	int granted = atomic_read(&pl->pl_granted);
	int grant_rate = atomic_read(&pl->pl_grant_rate);
	int cancel_rate = atomic_read(&pl->pl_cancel_rate);

	lprocfs_counter_add(pl->pl_stats, LDLM_POOL_SLV_STAT,
			    slv);
	lprocfs_counter_add(pl->pl_stats, LDLM_POOL_GRANTED_STAT,
			    granted);
	lprocfs_counter_add(pl->pl_stats, LDLM_POOL_GRANT_RATE_STAT,
			    grant_rate);
	lprocfs_counter_add(pl->pl_stats, LDLM_POOL_GRANT_PLAN_STAT,
			    grant_plan);
	lprocfs_counter_add(pl->pl_stats, LDLM_POOL_CANCEL_RATE_STAT,
			    cancel_rate);
}

/**
 * Sets current SLV into obd accessible via ldlm_pl2ns(pl)->ns_obd.
 */
static void ldlm_srv_pool_push_slv(struct ldlm_pool *pl)
{
	struct obd_device *obd;

	/*
	 * Set new SLV in obd field for using it later without accessing the
	 * pool. This is required to avoid race between sending reply to client
	 * with new SLV and cleanup server stack in which we can't guarantee
	 * that namespace is still alive. We know only that obd is alive as
	 * long as valid export is alive.
	 */
	obd = ldlm_pl2ns(pl)->ns_obd;
	LASSERT(obd != NULL);
	write_lock(&obd->obd_pool_lock);
	obd->obd_pool_slv = pl->pl_server_lock_volume;
	write_unlock(&obd->obd_pool_lock);
}

/**
 * Recalculates all pool fields on passed \a pl.
 *
 * \pre ->pl_lock is not locked.
 */
static int ldlm_srv_pool_recalc(struct ldlm_pool *pl)
{
	time_t recalc_interval_sec;

	recalc_interval_sec = get_seconds() - pl->pl_recalc_time;
	if (recalc_interval_sec < pl->pl_recalc_period)
		return 0;

	spin_lock(&pl->pl_lock);
	recalc_interval_sec = get_seconds() - pl->pl_recalc_time;
	if (recalc_interval_sec < pl->pl_recalc_period) {
		spin_unlock(&pl->pl_lock);
		return 0;
	}
	/*
	 * Recalc SLV after last period. This should be done
	 * _before_ recalculating new grant plan.
	 */
	ldlm_pool_recalc_slv(pl);

	/*
	 * Make sure that pool informed obd of last SLV changes.
	 */
	ldlm_srv_pool_push_slv(pl);

	/*
	 * Update grant_plan for new period.
	 */
	ldlm_pool_recalc_grant_plan(pl);

	pl->pl_recalc_time = get_seconds();
	lprocfs_counter_add(pl->pl_stats, LDLM_POOL_TIMING_STAT,
			    recalc_interval_sec);
	spin_unlock(&pl->pl_lock);
	return 0;
}

/**
 * This function is used on server side as main entry point for memory
 * pressure handling. It decreases SLV on \a pl according to passed
 * \a nr and \a gfp_mask.
 *
 * Our goal here is to decrease SLV such a way that clients hold \a nr
 * locks smaller in next 10h.
 */
static int ldlm_srv_pool_shrink(struct ldlm_pool *pl,
				int nr, gfp_t gfp_mask)
{
	__u32 limit;

	/*
	 * VM is asking how many entries may be potentially freed.
	 */
	if (nr == 0)
		return atomic_read(&pl->pl_granted);

	/*
	 * Client already canceled locks but server is already in shrinker
	 * and can't cancel anything. Let's catch this race.
	 */
	if (atomic_read(&pl->pl_granted) == 0)
		return 0;

	spin_lock(&pl->pl_lock);

	/*
	 * We want shrinker to possibly cause cancellation of @nr locks from
	 * clients or grant approximately @nr locks smaller next intervals.
	 *
	 * This is why we decreased SLV by @nr. This effect will only be as
	 * long as one re-calc interval (1s these days) and this should be
	 * enough to pass this decreased SLV to all clients. On next recalc
	 * interval pool will either increase SLV if locks load is not high
	 * or will keep on same level or even decrease again, thus, shrinker
	 * decreased SLV will affect next recalc intervals and this way will
	 * make locking load lower.
	 */
	if (nr < pl->pl_server_lock_volume) {
		pl->pl_server_lock_volume = pl->pl_server_lock_volume - nr;
	} else {
		limit = ldlm_pool_get_limit(pl);
		pl->pl_server_lock_volume = ldlm_pool_slv_min(limit);
	}

	/*
	 * Make sure that pool informed obd of last SLV changes.
	 */
	ldlm_srv_pool_push_slv(pl);
	spin_unlock(&pl->pl_lock);

	/*
	 * We did not really free any memory here so far, it only will be
	 * freed later may be, so that we return 0 to not confuse VM.
	 */
	return 0;
}

/**
 * Setup server side pool \a pl with passed \a limit.
 */
static int ldlm_srv_pool_setup(struct ldlm_pool *pl, int limit)
{
	struct obd_device *obd;

	obd = ldlm_pl2ns(pl)->ns_obd;
	LASSERT(obd != NULL && obd != LP_POISON);
	LASSERT(obd->obd_type != LP_POISON);
	write_lock(&obd->obd_pool_lock);
	obd->obd_pool_limit = limit;
	write_unlock(&obd->obd_pool_lock);

	ldlm_pool_set_limit(pl, limit);
	return 0;
}

/**
 * Sets SLV and Limit from ldlm_pl2ns(pl)->ns_obd tp passed \a pl.
 */
static void ldlm_cli_pool_pop_slv(struct ldlm_pool *pl)
{
	struct obd_device *obd;

	/*
	 * Get new SLV and Limit from obd which is updated with coming
	 * RPCs.
	 */
	obd = ldlm_pl2ns(pl)->ns_obd;
	LASSERT(obd != NULL);
	read_lock(&obd->obd_pool_lock);
	pl->pl_server_lock_volume = obd->obd_pool_slv;
	ldlm_pool_set_limit(pl, obd->obd_pool_limit);
	read_unlock(&obd->obd_pool_lock);
}

/**
 * Recalculates client size pool \a pl according to current SLV and Limit.
 */
static int ldlm_cli_pool_recalc(struct ldlm_pool *pl)
{
	time_t recalc_interval_sec;

	recalc_interval_sec = get_seconds() - pl->pl_recalc_time;
	if (recalc_interval_sec < pl->pl_recalc_period)
		return 0;

	spin_lock(&pl->pl_lock);
	/*
	 * Check if we need to recalc lists now.
	 */
	recalc_interval_sec = get_seconds() - pl->pl_recalc_time;
	if (recalc_interval_sec < pl->pl_recalc_period) {
		spin_unlock(&pl->pl_lock);
		return 0;
	}

	/*
	 * Make sure that pool knows last SLV and Limit from obd.
	 */
	ldlm_cli_pool_pop_slv(pl);

	pl->pl_recalc_time = get_seconds();
	lprocfs_counter_add(pl->pl_stats, LDLM_POOL_TIMING_STAT,
			    recalc_interval_sec);
	spin_unlock(&pl->pl_lock);

	/*
	 * Do not cancel locks in case lru resize is disabled for this ns.
	 */
	if (!ns_connect_lru_resize(ldlm_pl2ns(pl)))
		return 0;

	/*
	 * In the time of canceling locks on client we do not need to maintain
	 * sharp timing, we only want to cancel locks asap according to new SLV.
	 * It may be called when SLV has changed much, this is why we do not
	 * take into account pl->pl_recalc_time here.
	 */
	return ldlm_cancel_lru(ldlm_pl2ns(pl), 0, LCF_ASYNC, LDLM_CANCEL_LRUR);
}

/**
 * This function is main entry point for memory pressure handling on client
 * side.  Main goal of this function is to cancel some number of locks on
 * passed \a pl according to \a nr and \a gfp_mask.
 */
static int ldlm_cli_pool_shrink(struct ldlm_pool *pl,
				int nr, gfp_t gfp_mask)
{
	struct ldlm_namespace *ns;
	int unused;

	ns = ldlm_pl2ns(pl);

	/*
	 * Do not cancel locks in case lru resize is disabled for this ns.
	 */
	if (!ns_connect_lru_resize(ns))
		return 0;

	/*
	 * Make sure that pool knows last SLV and Limit from obd.
	 */
	ldlm_cli_pool_pop_slv(pl);

	spin_lock(&ns->ns_lock);
	unused = ns->ns_nr_unused;
	spin_unlock(&ns->ns_lock);

	if (nr == 0)
		return (unused / 100) * sysctl_vfs_cache_pressure;
	else
		return ldlm_cancel_lru(ns, nr, LCF_ASYNC, LDLM_CANCEL_SHRINK);
}

static const struct ldlm_pool_ops ldlm_srv_pool_ops = {
	.po_recalc = ldlm_srv_pool_recalc,
	.po_shrink = ldlm_srv_pool_shrink,
	.po_setup  = ldlm_srv_pool_setup
};

static const struct ldlm_pool_ops ldlm_cli_pool_ops = {
	.po_recalc = ldlm_cli_pool_recalc,
	.po_shrink = ldlm_cli_pool_shrink
};

/**
 * Pool recalc wrapper. Will call either client or server pool recalc callback
 * depending what pool \a pl is used.
 */
int ldlm_pool_recalc(struct ldlm_pool *pl)
{
	time_t recalc_interval_sec;
	int count;

	recalc_interval_sec = get_seconds() - pl->pl_recalc_time;
	if (recalc_interval_sec <= 0)
		goto recalc;

	spin_lock(&pl->pl_lock);
	if (recalc_interval_sec > 0) {
		/*
		 * Update pool statistics every 1s.
		 */
		ldlm_pool_recalc_stats(pl);

		/*
		 * Zero out all rates and speed for the last period.
		 */
		atomic_set(&pl->pl_grant_rate, 0);
		atomic_set(&pl->pl_cancel_rate, 0);
	}
	spin_unlock(&pl->pl_lock);

 recalc:
	if (pl->pl_ops->po_recalc != NULL) {
		count = pl->pl_ops->po_recalc(pl);
		lprocfs_counter_add(pl->pl_stats, LDLM_POOL_RECALC_STAT,
				    count);
	}
	recalc_interval_sec = pl->pl_recalc_time - get_seconds() +
			      pl->pl_recalc_period;

	return recalc_interval_sec;
}

/*
 * Pool shrink wrapper. Will call either client or server pool recalc callback
 * depending what pool pl is used. When nr == 0, just return the number of
 * freeable locks. Otherwise, return the number of canceled locks.
 */
int ldlm_pool_shrink(struct ldlm_pool *pl, int nr,
		     gfp_t gfp_mask)
{
	int cancel = 0;

	if (pl->pl_ops->po_shrink != NULL) {
		cancel = pl->pl_ops->po_shrink(pl, nr, gfp_mask);
		if (nr > 0) {
			lprocfs_counter_add(pl->pl_stats,
					    LDLM_POOL_SHRINK_REQTD_STAT,
					    nr);
			lprocfs_counter_add(pl->pl_stats,
					    LDLM_POOL_SHRINK_FREED_STAT,
					    cancel);
			CDEBUG(D_DLMTRACE, "%s: request to shrink %d locks, "
			       "shrunk %d\n", pl->pl_name, nr, cancel);
		}
	}
	return cancel;
}
EXPORT_SYMBOL(ldlm_pool_shrink);

/**
 * Pool setup wrapper. Will call either client or server pool recalc callback
 * depending what pool \a pl is used.
 *
 * Sets passed \a limit into pool \a pl.
 */
int ldlm_pool_setup(struct ldlm_pool *pl, int limit)
{
	if (pl->pl_ops->po_setup != NULL)
		return(pl->pl_ops->po_setup(pl, limit));
	return 0;
}
EXPORT_SYMBOL(ldlm_pool_setup);

#if defined (CONFIG_PROC_FS)
static int lprocfs_pool_state_seq_show(struct seq_file *m, void *unused)
{
	int granted, grant_rate, cancel_rate, grant_step;
	int grant_speed, grant_plan, lvf;
	struct ldlm_pool *pl = m->private;
	__u64 slv, clv;
	__u32 limit;

	spin_lock(&pl->pl_lock);
	slv = pl->pl_server_lock_volume;
	clv = pl->pl_client_lock_volume;
	limit = ldlm_pool_get_limit(pl);
	grant_plan = pl->pl_grant_plan;
	granted = atomic_read(&pl->pl_granted);
	grant_rate = atomic_read(&pl->pl_grant_rate);
	cancel_rate = atomic_read(&pl->pl_cancel_rate);
	grant_speed = grant_rate - cancel_rate;
	lvf = atomic_read(&pl->pl_lock_volume_factor);
	grant_step = ldlm_pool_t2gsp(pl->pl_recalc_period);
	spin_unlock(&pl->pl_lock);

	seq_printf(m, "LDLM pool state (%s):\n"
		      "  SLV: %llu\n"
		      "  CLV: %llu\n"
		      "  LVF: %d\n",
		      pl->pl_name, slv, clv, lvf);

	if (ns_is_server(ldlm_pl2ns(pl))) {
		seq_printf(m, "  GSP: %d%%\n"
			      "  GP:  %d\n",
			      grant_step, grant_plan);
	}
	seq_printf(m, "  GR:  %d\n" "  CR:  %d\n" "  GS:  %d\n"
		      "  G:   %d\n" "  L:   %d\n",
		      grant_rate, cancel_rate, grant_speed,
		      granted, limit);

	return 0;
}
LPROC_SEQ_FOPS_RO(lprocfs_pool_state);

static int lprocfs_grant_speed_seq_show(struct seq_file *m, void *unused)
{
	struct ldlm_pool *pl = m->private;
	int	       grant_speed;

	spin_lock(&pl->pl_lock);
	/* serialize with ldlm_pool_recalc */
	grant_speed = atomic_read(&pl->pl_grant_rate) -
			atomic_read(&pl->pl_cancel_rate);
	spin_unlock(&pl->pl_lock);
	return lprocfs_rd_uint(m, &grant_speed);
}

LDLM_POOL_PROC_READER_SEQ_SHOW(grant_plan, int);
LPROC_SEQ_FOPS_RO(lprocfs_grant_plan);

LDLM_POOL_PROC_READER_SEQ_SHOW(recalc_period, int);
LDLM_POOL_PROC_WRITER(recalc_period, int);
static ssize_t lprocfs_recalc_period_seq_write(struct file *file, const char *buf,
					   size_t len, loff_t *off)
{
	struct seq_file *seq = file->private_data;

	return lprocfs_wr_recalc_period(file, buf, len, seq->private);
}
LPROC_SEQ_FOPS(lprocfs_recalc_period);

LPROC_SEQ_FOPS_RO_TYPE(ldlm_pool, u64);
LPROC_SEQ_FOPS_RO_TYPE(ldlm_pool, atomic);
LPROC_SEQ_FOPS_RW_TYPE(ldlm_pool_rw, atomic);

LPROC_SEQ_FOPS_RO(lprocfs_grant_speed);

#define LDLM_POOL_ADD_VAR(name, var, ops)			\
	do {							\
		snprintf(var_name, MAX_STRING_SIZE, #name);	\
		pool_vars[0].data = var;			\
		pool_vars[0].fops = ops;			\
		lprocfs_add_vars(pl->pl_proc_dir, pool_vars, NULL);\
	} while (0)

static int ldlm_pool_proc_init(struct ldlm_pool *pl)
{
	struct ldlm_namespace *ns = ldlm_pl2ns(pl);
	struct proc_dir_entry *parent_ns_proc;
	struct lprocfs_vars pool_vars[2];
	char *var_name = NULL;
	int rc = 0;

	OBD_ALLOC(var_name, MAX_STRING_SIZE + 1);
	if (!var_name)
		return -ENOMEM;

	parent_ns_proc = ns->ns_proc_dir_entry;
	if (parent_ns_proc == NULL) {
		CERROR("%s: proc entry is not initialized\n",
		       ldlm_ns_name(ns));
		GOTO(out_free_name, rc = -EINVAL);
	}
	pl->pl_proc_dir = lprocfs_register("pool", parent_ns_proc,
					   NULL, NULL);
	if (IS_ERR(pl->pl_proc_dir)) {
		CERROR("LProcFS failed in ldlm-pool-init\n");
		rc = PTR_ERR(pl->pl_proc_dir);
		pl->pl_proc_dir = NULL;
		GOTO(out_free_name, rc);
	}

	var_name[MAX_STRING_SIZE] = '\0';
	memset(pool_vars, 0, sizeof(pool_vars));
	pool_vars[0].name = var_name;

	LDLM_POOL_ADD_VAR("server_lock_volume", &pl->pl_server_lock_volume,
			  &ldlm_pool_u64_fops);
	LDLM_POOL_ADD_VAR("limit", &pl->pl_limit, &ldlm_pool_rw_atomic_fops);
	LDLM_POOL_ADD_VAR("granted", &pl->pl_granted, &ldlm_pool_atomic_fops);
	LDLM_POOL_ADD_VAR("grant_speed", pl, &lprocfs_grant_speed_fops);
	LDLM_POOL_ADD_VAR("cancel_rate", &pl->pl_cancel_rate,
			  &ldlm_pool_atomic_fops);
	LDLM_POOL_ADD_VAR("grant_rate", &pl->pl_grant_rate,
			  &ldlm_pool_atomic_fops);
	LDLM_POOL_ADD_VAR("grant_plan", pl, &lprocfs_grant_plan_fops);
	LDLM_POOL_ADD_VAR("recalc_period", pl, &lprocfs_recalc_period_fops);
	LDLM_POOL_ADD_VAR("lock_volume_factor", &pl->pl_lock_volume_factor,
			  &ldlm_pool_rw_atomic_fops);
	LDLM_POOL_ADD_VAR("state", pl, &lprocfs_pool_state_fops);

	pl->pl_stats = lprocfs_alloc_stats(LDLM_POOL_LAST_STAT -
					   LDLM_POOL_FIRST_STAT, 0);
	if (!pl->pl_stats)
		GOTO(out_free_name, rc = -ENOMEM);

	lprocfs_counter_init(pl->pl_stats, LDLM_POOL_GRANTED_STAT,
			     LPROCFS_CNTR_AVGMINMAX | LPROCFS_CNTR_STDDEV,
			     "granted", "locks");
	lprocfs_counter_init(pl->pl_stats, LDLM_POOL_GRANT_STAT,
			     LPROCFS_CNTR_AVGMINMAX | LPROCFS_CNTR_STDDEV,
			     "grant", "locks");
	lprocfs_counter_init(pl->pl_stats, LDLM_POOL_CANCEL_STAT,
			     LPROCFS_CNTR_AVGMINMAX | LPROCFS_CNTR_STDDEV,
			     "cancel", "locks");
	lprocfs_counter_init(pl->pl_stats, LDLM_POOL_GRANT_RATE_STAT,
			     LPROCFS_CNTR_AVGMINMAX | LPROCFS_CNTR_STDDEV,
			     "grant_rate", "locks/s");
	lprocfs_counter_init(pl->pl_stats, LDLM_POOL_CANCEL_RATE_STAT,
			     LPROCFS_CNTR_AVGMINMAX | LPROCFS_CNTR_STDDEV,
			     "cancel_rate", "locks/s");
	lprocfs_counter_init(pl->pl_stats, LDLM_POOL_GRANT_PLAN_STAT,
			     LPROCFS_CNTR_AVGMINMAX | LPROCFS_CNTR_STDDEV,
			     "grant_plan", "locks/s");
	lprocfs_counter_init(pl->pl_stats, LDLM_POOL_SLV_STAT,
			     LPROCFS_CNTR_AVGMINMAX | LPROCFS_CNTR_STDDEV,
			     "slv", "slv");
	lprocfs_counter_init(pl->pl_stats, LDLM_POOL_SHRINK_REQTD_STAT,
			     LPROCFS_CNTR_AVGMINMAX | LPROCFS_CNTR_STDDEV,
			     "shrink_request", "locks");
	lprocfs_counter_init(pl->pl_stats, LDLM_POOL_SHRINK_FREED_STAT,
			     LPROCFS_CNTR_AVGMINMAX | LPROCFS_CNTR_STDDEV,
			     "shrink_freed", "locks");
	lprocfs_counter_init(pl->pl_stats, LDLM_POOL_RECALC_STAT,
			     LPROCFS_CNTR_AVGMINMAX | LPROCFS_CNTR_STDDEV,
			     "recalc_freed", "locks");
	lprocfs_counter_init(pl->pl_stats, LDLM_POOL_TIMING_STAT,
			     LPROCFS_CNTR_AVGMINMAX | LPROCFS_CNTR_STDDEV,
			     "recalc_timing", "sec");
	rc = lprocfs_register_stats(pl->pl_proc_dir, "stats", pl->pl_stats);

out_free_name:
	OBD_FREE(var_name, MAX_STRING_SIZE + 1);
	return rc;
}

static void ldlm_pool_proc_fini(struct ldlm_pool *pl)
{
	if (pl->pl_stats != NULL) {
		lprocfs_free_stats(&pl->pl_stats);
		pl->pl_stats = NULL;
	}
	if (pl->pl_proc_dir != NULL) {
		lprocfs_remove(&pl->pl_proc_dir);
		pl->pl_proc_dir = NULL;
	}
}
#else /* !CONFIG_PROC_FS */
static int ldlm_pool_proc_init(struct ldlm_pool *pl)
{
	return 0;
}

static void ldlm_pool_proc_fini(struct ldlm_pool *pl) {}
#endif /* CONFIG_PROC_FS */

int ldlm_pool_init(struct ldlm_pool *pl, struct ldlm_namespace *ns,
		   int idx, ldlm_side_t client)
{
	int rc;

	spin_lock_init(&pl->pl_lock);
	atomic_set(&pl->pl_granted, 0);
	pl->pl_recalc_time = get_seconds();
	atomic_set(&pl->pl_lock_volume_factor, 1);

	atomic_set(&pl->pl_grant_rate, 0);
	atomic_set(&pl->pl_cancel_rate, 0);
	pl->pl_grant_plan = LDLM_POOL_GP(LDLM_POOL_HOST_L);

	snprintf(pl->pl_name, sizeof(pl->pl_name), "ldlm-pool-%s-%d",
		 ldlm_ns_name(ns), idx);

	if (client == LDLM_NAMESPACE_SERVER) {
		pl->pl_ops = &ldlm_srv_pool_ops;
		ldlm_pool_set_limit(pl, LDLM_POOL_HOST_L);
		pl->pl_recalc_period = LDLM_POOL_SRV_DEF_RECALC_PERIOD;
		pl->pl_server_lock_volume = ldlm_pool_slv_max(LDLM_POOL_HOST_L);
	} else {
		ldlm_pool_set_limit(pl, 1);
		pl->pl_server_lock_volume = 0;
		pl->pl_ops = &ldlm_cli_pool_ops;
		pl->pl_recalc_period = LDLM_POOL_CLI_DEF_RECALC_PERIOD;
	}
	pl->pl_client_lock_volume = 0;
	rc = ldlm_pool_proc_init(pl);
	if (rc)
		return rc;

	CDEBUG(D_DLMTRACE, "Lock pool %s is initialized\n", pl->pl_name);

	return rc;
}
EXPORT_SYMBOL(ldlm_pool_init);

void ldlm_pool_fini(struct ldlm_pool *pl)
{
	ldlm_pool_proc_fini(pl);

	/*
	 * Pool should not be used after this point. We can't free it here as
	 * it lives in struct ldlm_namespace, but still interested in catching
	 * any abnormal using cases.
	 */
	POISON(pl, 0x5a, sizeof(*pl));
}
EXPORT_SYMBOL(ldlm_pool_fini);

/**
 * Add new taken ldlm lock \a lock into pool \a pl accounting.
 */
void ldlm_pool_add(struct ldlm_pool *pl, struct ldlm_lock *lock)
{
	/*
	 * FLOCK locks are special in a sense that they are almost never
	 * cancelled, instead special kind of lock is used to drop them.
	 * also there is no LRU for flock locks, so no point in tracking
	 * them anyway.
	 */
	if (lock->l_resource->lr_type == LDLM_FLOCK)
		return;

	atomic_inc(&pl->pl_granted);
	atomic_inc(&pl->pl_grant_rate);
	lprocfs_counter_incr(pl->pl_stats, LDLM_POOL_GRANT_STAT);
	/*
	 * Do not do pool recalc for client side as all locks which
	 * potentially may be canceled has already been packed into
	 * enqueue/cancel rpc. Also we do not want to run out of stack
	 * with too long call paths.
	 */
	if (ns_is_server(ldlm_pl2ns(pl)))
		ldlm_pool_recalc(pl);
}
EXPORT_SYMBOL(ldlm_pool_add);

/**
 * Remove ldlm lock \a lock from pool \a pl accounting.
 */
void ldlm_pool_del(struct ldlm_pool *pl, struct ldlm_lock *lock)
{
	/*
	 * Filter out FLOCK locks. Read above comment in ldlm_pool_add().
	 */
	if (lock->l_resource->lr_type == LDLM_FLOCK)
		return;

	LASSERT(atomic_read(&pl->pl_granted) > 0);
	atomic_dec(&pl->pl_granted);
	atomic_inc(&pl->pl_cancel_rate);

	lprocfs_counter_incr(pl->pl_stats, LDLM_POOL_CANCEL_STAT);

	if (ns_is_server(ldlm_pl2ns(pl)))
		ldlm_pool_recalc(pl);
}
EXPORT_SYMBOL(ldlm_pool_del);

/**
 * Returns current \a pl SLV.
 *
 * \pre ->pl_lock is not locked.
 */
__u64 ldlm_pool_get_slv(struct ldlm_pool *pl)
{
	__u64 slv;
	spin_lock(&pl->pl_lock);
	slv = pl->pl_server_lock_volume;
	spin_unlock(&pl->pl_lock);
	return slv;
}
EXPORT_SYMBOL(ldlm_pool_get_slv);

/**
 * Sets passed \a slv to \a pl.
 *
 * \pre ->pl_lock is not locked.
 */
void ldlm_pool_set_slv(struct ldlm_pool *pl, __u64 slv)
{
	spin_lock(&pl->pl_lock);
	pl->pl_server_lock_volume = slv;
	spin_unlock(&pl->pl_lock);
}
EXPORT_SYMBOL(ldlm_pool_set_slv);

/**
 * Returns current \a pl CLV.
 *
 * \pre ->pl_lock is not locked.
 */
__u64 ldlm_pool_get_clv(struct ldlm_pool *pl)
{
	__u64 slv;
	spin_lock(&pl->pl_lock);
	slv = pl->pl_client_lock_volume;
	spin_unlock(&pl->pl_lock);
	return slv;
}
EXPORT_SYMBOL(ldlm_pool_get_clv);

/**
 * Sets passed \a clv to \a pl.
 *
 * \pre ->pl_lock is not locked.
 */
void ldlm_pool_set_clv(struct ldlm_pool *pl, __u64 clv)
{
	spin_lock(&pl->pl_lock);
	pl->pl_client_lock_volume = clv;
	spin_unlock(&pl->pl_lock);
}
EXPORT_SYMBOL(ldlm_pool_set_clv);

/**
 * Returns current \a pl limit.
 */
__u32 ldlm_pool_get_limit(struct ldlm_pool *pl)
{
	return atomic_read(&pl->pl_limit);
}
EXPORT_SYMBOL(ldlm_pool_get_limit);

/**
 * Sets passed \a limit to \a pl.
 */
void ldlm_pool_set_limit(struct ldlm_pool *pl, __u32 limit)
{
	atomic_set(&pl->pl_limit, limit);
}
EXPORT_SYMBOL(ldlm_pool_set_limit);

/**
 * Returns current LVF from \a pl.
 */
__u32 ldlm_pool_get_lvf(struct ldlm_pool *pl)
{
	return atomic_read(&pl->pl_lock_volume_factor);
}
EXPORT_SYMBOL(ldlm_pool_get_lvf);

static int ldlm_pool_granted(struct ldlm_pool *pl)
{
	return atomic_read(&pl->pl_granted);
}

static struct ptlrpc_thread *ldlm_pools_thread;
static struct completion ldlm_pools_comp;

/*
 * count locks from all namespaces (if possible). Returns number of
 * cached locks.
 */
static unsigned long ldlm_pools_count(ldlm_side_t client, gfp_t gfp_mask)
{
	int total = 0, nr_ns;
	struct ldlm_namespace *ns;
	struct ldlm_namespace *ns_old = NULL; /* loop detection */
	void *cookie;

	if (client == LDLM_NAMESPACE_CLIENT && !(gfp_mask & __GFP_FS))
		return 0;

	CDEBUG(D_DLMTRACE, "Request to count %s locks from all pools\n",
	       client == LDLM_NAMESPACE_CLIENT ? "client" : "server");

	cookie = cl_env_reenter();

	/*
	 * Find out how many resources we may release.
	 */
	for (nr_ns = ldlm_namespace_nr_read(client);
	     nr_ns > 0; nr_ns--) {
		mutex_lock(ldlm_namespace_lock(client));
		if (list_empty(ldlm_namespace_list(client))) {
			mutex_unlock(ldlm_namespace_lock(client));
			cl_env_reexit(cookie);
			return 0;
		}
		ns = ldlm_namespace_first_locked(client);

		if (ns == ns_old) {
			mutex_unlock(ldlm_namespace_lock(client));
			break;
		}

		if (ldlm_ns_empty(ns)) {
			ldlm_namespace_move_to_inactive_locked(ns, client);
			mutex_unlock(ldlm_namespace_lock(client));
			continue;
		}

		if (ns_old == NULL)
			ns_old = ns;

		ldlm_namespace_get(ns);
		ldlm_namespace_move_to_active_locked(ns, client);
		mutex_unlock(ldlm_namespace_lock(client));
		total += ldlm_pool_shrink(&ns->ns_pool, 0, gfp_mask);
		ldlm_namespace_put(ns);
	}

	cl_env_reexit(cookie);
	return total;
}

static unsigned long ldlm_pools_scan(ldlm_side_t client, int nr, gfp_t gfp_mask)
{
	unsigned long freed = 0;
	int tmp, nr_ns;
	struct ldlm_namespace *ns;
	void *cookie;

	if (client == LDLM_NAMESPACE_CLIENT && !(gfp_mask & __GFP_FS))
		return -1;

	cookie = cl_env_reenter();

	/*
	 * Shrink at least ldlm_namespace_nr_read(client) namespaces.
	 */
	for (tmp = nr_ns = ldlm_namespace_nr_read(client);
	     tmp > 0; tmp--) {
		int cancel, nr_locks;

		/*
		 * Do not call shrink under ldlm_namespace_lock(client)
		 */
		mutex_lock(ldlm_namespace_lock(client));
		if (list_empty(ldlm_namespace_list(client))) {
			mutex_unlock(ldlm_namespace_lock(client));
			break;
		}
		ns = ldlm_namespace_first_locked(client);
		ldlm_namespace_get(ns);
		ldlm_namespace_move_to_active_locked(ns, client);
		mutex_unlock(ldlm_namespace_lock(client));

		nr_locks = ldlm_pool_granted(&ns->ns_pool);
		/*
		 * We use to shrink propotionally but with new shrinker API,
		 * we lost the total number of freeable locks.
		 */
		cancel = 1 + min_t(int, nr_locks, nr / nr_ns);
		freed += ldlm_pool_shrink(&ns->ns_pool, cancel, gfp_mask);
		ldlm_namespace_put(ns);
	}
	cl_env_reexit(cookie);
	/*
	 * we only decrease the SLV in server pools shrinker, return
	 * SHRINK_STOP to kernel to avoid needless loop. LU-1128
	 */
	return (client == LDLM_NAMESPACE_SERVER) ? SHRINK_STOP : freed;
}

static unsigned long ldlm_pools_srv_count(struct shrinker *s, struct shrink_control *sc)
{
	return ldlm_pools_count(LDLM_NAMESPACE_SERVER, sc->gfp_mask);
}

static unsigned long ldlm_pools_srv_scan(struct shrinker *s, struct shrink_control *sc)
{
	return ldlm_pools_scan(LDLM_NAMESPACE_SERVER, sc->nr_to_scan,
			       sc->gfp_mask);
}

static unsigned long ldlm_pools_cli_count(struct shrinker *s, struct shrink_control *sc)
{
	return ldlm_pools_count(LDLM_NAMESPACE_CLIENT, sc->gfp_mask);
}

static unsigned long ldlm_pools_cli_scan(struct shrinker *s, struct shrink_control *sc)
{
	return ldlm_pools_scan(LDLM_NAMESPACE_CLIENT, sc->nr_to_scan,
			       sc->gfp_mask);
}

int ldlm_pools_recalc(ldlm_side_t client)
{
	__u32 nr_l = 0, nr_p = 0, l;
	struct ldlm_namespace *ns;
	struct ldlm_namespace *ns_old = NULL;
	int nr, equal = 0;
	int time = 50; /* seconds of sleep if no active namespaces */

	/*
	 * No need to setup pool limit for client pools.
	 */
	if (client == LDLM_NAMESPACE_SERVER) {
		/*
		 * Check all modest namespaces first.
		 */
		mutex_lock(ldlm_namespace_lock(client));
		list_for_each_entry(ns, ldlm_namespace_list(client),
					ns_list_chain)
		{
			if (ns->ns_appetite != LDLM_NAMESPACE_MODEST)
				continue;

			l = ldlm_pool_granted(&ns->ns_pool);
			if (l == 0)
				l = 1;

			/*
			 * Set the modest pools limit equal to their avg granted
			 * locks + ~6%.
			 */
			l += dru(l, LDLM_POOLS_MODEST_MARGIN_SHIFT, 0);
			ldlm_pool_setup(&ns->ns_pool, l);
			nr_l += l;
			nr_p++;
		}

		/*
		 * Make sure that modest namespaces did not eat more that 2/3
		 * of limit.
		 */
		if (nr_l >= 2 * (LDLM_POOL_HOST_L / 3)) {
			CWARN("\"Modest\" pools eat out 2/3 of server locks "
			      "limit (%d of %lu). This means that you have too "
			      "many clients for this amount of server RAM. "
			      "Upgrade server!\n", nr_l, LDLM_POOL_HOST_L);
			equal = 1;
		}

		/*
		 * The rest is given to greedy namespaces.
		 */
		list_for_each_entry(ns, ldlm_namespace_list(client),
					ns_list_chain)
		{
			if (!equal && ns->ns_appetite != LDLM_NAMESPACE_GREEDY)
				continue;

			if (equal) {
				/*
				 * In the case 2/3 locks are eaten out by
				 * modest pools, we re-setup equal limit
				 * for _all_ pools.
				 */
				l = LDLM_POOL_HOST_L /
					ldlm_namespace_nr_read(client);
			} else {
				/*
				 * All the rest of greedy pools will have
				 * all locks in equal parts.
				 */
				l = (LDLM_POOL_HOST_L - nr_l) /
					(ldlm_namespace_nr_read(client) -
					 nr_p);
			}
			ldlm_pool_setup(&ns->ns_pool, l);
		}
		mutex_unlock(ldlm_namespace_lock(client));
	}

	/*
	 * Recalc at least ldlm_namespace_nr_read(client) namespaces.
	 */
	for (nr = ldlm_namespace_nr_read(client); nr > 0; nr--) {
		int     skip;
		/*
		 * Lock the list, get first @ns in the list, getref, move it
		 * to the tail, unlock and call pool recalc. This way we avoid
		 * calling recalc under @ns lock what is really good as we get
		 * rid of potential deadlock on client nodes when canceling
		 * locks synchronously.
		 */
		mutex_lock(ldlm_namespace_lock(client));
		if (list_empty(ldlm_namespace_list(client))) {
			mutex_unlock(ldlm_namespace_lock(client));
			break;
		}
		ns = ldlm_namespace_first_locked(client);

		if (ns_old == ns) { /* Full pass complete */
			mutex_unlock(ldlm_namespace_lock(client));
			break;
		}

		/* We got an empty namespace, need to move it back to inactive
		 * list.
		 * The race with parallel resource creation is fine:
		 * - If they do namespace_get before our check, we fail the
		 *   check and they move this item to the end of the list anyway
		 * - If we do the check and then they do namespace_get, then
		 *   we move the namespace to inactive and they will move
		 *   it back to active (synchronised by the lock, so no clash
		 *   there).
		 */
		if (ldlm_ns_empty(ns)) {
			ldlm_namespace_move_to_inactive_locked(ns, client);
			mutex_unlock(ldlm_namespace_lock(client));
			continue;
		}

		if (ns_old == NULL)
			ns_old = ns;

		spin_lock(&ns->ns_lock);
		/*
		 * skip ns which is being freed, and we don't want to increase
		 * its refcount again, not even temporarily. bz21519 & LU-499.
		 */
		if (ns->ns_stopping) {
			skip = 1;
		} else {
			skip = 0;
			ldlm_namespace_get(ns);
		}
		spin_unlock(&ns->ns_lock);

		ldlm_namespace_move_to_active_locked(ns, client);
		mutex_unlock(ldlm_namespace_lock(client));

		/*
		 * After setup is done - recalc the pool.
		 */
		if (!skip) {
			int ttime = ldlm_pool_recalc(&ns->ns_pool);

			if (ttime < time)
				time = ttime;

			ldlm_namespace_put(ns);
		}
	}
	return time;
}
EXPORT_SYMBOL(ldlm_pools_recalc);

static int ldlm_pools_thread_main(void *arg)
{
	struct ptlrpc_thread *thread = (struct ptlrpc_thread *)arg;
	int s_time, c_time;

	thread_set_flags(thread, SVC_RUNNING);
	wake_up(&thread->t_ctl_waitq);

	CDEBUG(D_DLMTRACE, "%s: pool thread starting, process %d\n",
		"ldlm_poold", current_pid());

	while (1) {
		struct l_wait_info lwi;

		/*
		 * Recal all pools on this tick.
		 */
		s_time = ldlm_pools_recalc(LDLM_NAMESPACE_SERVER);
		c_time = ldlm_pools_recalc(LDLM_NAMESPACE_CLIENT);

		/*
		 * Wait until the next check time, or until we're
		 * stopped.
		 */
		lwi = LWI_TIMEOUT(cfs_time_seconds(min(s_time, c_time)),
				  NULL, NULL);
		l_wait_event(thread->t_ctl_waitq,
			     thread_is_stopping(thread) ||
			     thread_is_event(thread),
			     &lwi);

		if (thread_test_and_clear_flags(thread, SVC_STOPPING))
			break;
		else
			thread_test_and_clear_flags(thread, SVC_EVENT);
	}

	thread_set_flags(thread, SVC_STOPPED);
	wake_up(&thread->t_ctl_waitq);

	CDEBUG(D_DLMTRACE, "%s: pool thread exiting, process %d\n",
		"ldlm_poold", current_pid());

	complete_and_exit(&ldlm_pools_comp, 0);
}

static int ldlm_pools_thread_start(void)
{
	struct l_wait_info lwi = { 0 };
	struct task_struct *task;

	if (ldlm_pools_thread != NULL)
		return -EALREADY;

	OBD_ALLOC_PTR(ldlm_pools_thread);
	if (ldlm_pools_thread == NULL)
		return -ENOMEM;

	init_completion(&ldlm_pools_comp);
	init_waitqueue_head(&ldlm_pools_thread->t_ctl_waitq);

	task = kthread_run(ldlm_pools_thread_main, ldlm_pools_thread,
			   "ldlm_poold");
	if (IS_ERR(task)) {
		CERROR("Can't start pool thread, error %ld\n", PTR_ERR(task));
		OBD_FREE(ldlm_pools_thread, sizeof(*ldlm_pools_thread));
		ldlm_pools_thread = NULL;
		return PTR_ERR(task);
	}
	l_wait_event(ldlm_pools_thread->t_ctl_waitq,
		     thread_is_running(ldlm_pools_thread), &lwi);
	return 0;
}

static void ldlm_pools_thread_stop(void)
{
	if (ldlm_pools_thread == NULL) {
		return;
	}

	thread_set_flags(ldlm_pools_thread, SVC_STOPPING);
	wake_up(&ldlm_pools_thread->t_ctl_waitq);

	/*
	 * Make sure that pools thread is finished before freeing @thread.
	 * This fixes possible race and oops due to accessing freed memory
	 * in pools thread.
	 */
	wait_for_completion(&ldlm_pools_comp);
	OBD_FREE_PTR(ldlm_pools_thread);
	ldlm_pools_thread = NULL;
}

static struct shrinker ldlm_pools_srv_shrinker = {
	.count_objects	= ldlm_pools_srv_count,
	.scan_objects	= ldlm_pools_srv_scan,
	.seeks		= DEFAULT_SEEKS,
};

static struct shrinker ldlm_pools_cli_shrinker = {
	.count_objects	= ldlm_pools_cli_count,
	.scan_objects	= ldlm_pools_cli_scan,
	.seeks		= DEFAULT_SEEKS,
};

int ldlm_pools_init(void)
{
	int rc;

	rc = ldlm_pools_thread_start();
	if (rc == 0) {
		register_shrinker(&ldlm_pools_srv_shrinker);
		register_shrinker(&ldlm_pools_cli_shrinker);
	}
	return rc;
}
EXPORT_SYMBOL(ldlm_pools_init);

void ldlm_pools_fini(void)
{
	unregister_shrinker(&ldlm_pools_srv_shrinker);
	unregister_shrinker(&ldlm_pools_cli_shrinker);
	ldlm_pools_thread_stop();
}
EXPORT_SYMBOL(ldlm_pools_fini);
