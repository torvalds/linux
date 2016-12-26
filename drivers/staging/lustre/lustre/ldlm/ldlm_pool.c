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
 * http://www.gnu.org/licenses/gpl-2.0.html
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2007, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2010, 2015, Intel Corporation.
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
 * Client has LVF, that is, lock volume factor which regulates how much
 * sensitive client should be about last SLV from server. The higher LVF is the
 * more locks will be canceled on client. Default value for it is 1. Setting LVF
 * to 2 means that client will cancel locks 2 times faster.
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
 * General idea of this belongs to Vitaly Fertman (vitaly@clusterfs.com).
 * Andreas Dilger (adilger@clusterfs.com) proposed few nice ideas like using
 * LVF and many cleanups. Flow definition to allow more easy understanding of
 * the logic belongs to Nikita Danilov (nikita@clusterfs.com) as well as many
 * cleanups and fixes. And design and implementation are done by Yury Umanets
 * (umka@clusterfs.com).
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
 * affect behavior much. They all may be modified via sysfs. However, they also
 * give a possibility for constructing few pre-defined behavior policies. If
 * none of predefines is suitable for a working pattern being used, new one may
 * be "constructed" via sysfs tunables.
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
#define LDLM_POOL_HOST_L ((NUM_CACHEPAGES >> (20 - PAGE_SHIFT)) * 50)

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
 * Sets SLV and Limit from container_of(pl, struct ldlm_namespace,
 * ns_pool)->ns_obd tp passed \a pl.
 */
static void ldlm_cli_pool_pop_slv(struct ldlm_pool *pl)
{
	struct obd_device *obd;

	/*
	 * Get new SLV and Limit from obd which is updated with coming
	 * RPCs.
	 */
	obd = container_of(pl, struct ldlm_namespace,
			   ns_pool)->ns_obd;
	read_lock(&obd->obd_pool_lock);
	pl->pl_server_lock_volume = obd->obd_pool_slv;
	atomic_set(&pl->pl_limit, obd->obd_pool_limit);
	read_unlock(&obd->obd_pool_lock);
}

/**
 * Recalculates client size pool \a pl according to current SLV and Limit.
 */
static int ldlm_cli_pool_recalc(struct ldlm_pool *pl)
{
	time64_t recalc_interval_sec;
	int ret;

	recalc_interval_sec = ktime_get_real_seconds() - pl->pl_recalc_time;
	if (recalc_interval_sec < pl->pl_recalc_period)
		return 0;

	spin_lock(&pl->pl_lock);
	/*
	 * Check if we need to recalc lists now.
	 */
	recalc_interval_sec = ktime_get_real_seconds() - pl->pl_recalc_time;
	if (recalc_interval_sec < pl->pl_recalc_period) {
		spin_unlock(&pl->pl_lock);
		return 0;
	}

	/*
	 * Make sure that pool knows last SLV and Limit from obd.
	 */
	ldlm_cli_pool_pop_slv(pl);

	spin_unlock(&pl->pl_lock);

	/*
	 * Do not cancel locks in case lru resize is disabled for this ns.
	 */
	if (!ns_connect_lru_resize(container_of(pl, struct ldlm_namespace,
						ns_pool))) {
		ret = 0;
		goto out;
	}

	/*
	 * In the time of canceling locks on client we do not need to maintain
	 * sharp timing, we only want to cancel locks asap according to new SLV.
	 * It may be called when SLV has changed much, this is why we do not
	 * take into account pl->pl_recalc_time here.
	 */
	ret = ldlm_cancel_lru(container_of(pl, struct ldlm_namespace, ns_pool),
			      0, LCF_ASYNC, LDLM_CANCEL_LRUR);

out:
	spin_lock(&pl->pl_lock);
	/*
	 * Time of LRU resizing might be longer than period,
	 * so update after LRU resizing rather than before it.
	 */
	pl->pl_recalc_time = ktime_get_real_seconds();
	lprocfs_counter_add(pl->pl_stats, LDLM_POOL_TIMING_STAT,
			    recalc_interval_sec);
	spin_unlock(&pl->pl_lock);
	return ret;
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

	ns = container_of(pl, struct ldlm_namespace, ns_pool);

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

static const struct ldlm_pool_ops ldlm_cli_pool_ops = {
	.po_recalc = ldlm_cli_pool_recalc,
	.po_shrink = ldlm_cli_pool_shrink
};

/**
 * Pool recalc wrapper. Will call either client or server pool recalc callback
 * depending what pool \a pl is used.
 */
static int ldlm_pool_recalc(struct ldlm_pool *pl)
{
	u32 recalc_interval_sec;
	int count;

	recalc_interval_sec = ktime_get_seconds() - pl->pl_recalc_time;
	if (recalc_interval_sec > 0) {
		spin_lock(&pl->pl_lock);
		recalc_interval_sec = ktime_get_seconds() - pl->pl_recalc_time;

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
	}

	if (pl->pl_ops->po_recalc) {
		count = pl->pl_ops->po_recalc(pl);
		lprocfs_counter_add(pl->pl_stats, LDLM_POOL_RECALC_STAT,
				    count);
	}

	recalc_interval_sec = pl->pl_recalc_time - ktime_get_seconds() +
			      pl->pl_recalc_period;
	if (recalc_interval_sec <= 0) {
		/* DEBUG: should be re-removed after LU-4536 is fixed */
		CDEBUG(D_DLMTRACE, "%s: Negative interval(%ld), too short period(%ld)\n",
		       pl->pl_name, (long)recalc_interval_sec,
		       (long)pl->pl_recalc_period);

		/* Prevent too frequent recalculation. */
		recalc_interval_sec = 1;
	}

	return recalc_interval_sec;
}

/*
 * Pool shrink wrapper. Will call either client or server pool recalc callback
 * depending what pool pl is used. When nr == 0, just return the number of
 * freeable locks. Otherwise, return the number of canceled locks.
 */
static int ldlm_pool_shrink(struct ldlm_pool *pl, int nr, gfp_t gfp_mask)
{
	int cancel = 0;

	if (pl->pl_ops->po_shrink) {
		cancel = pl->pl_ops->po_shrink(pl, nr, gfp_mask);
		if (nr > 0) {
			lprocfs_counter_add(pl->pl_stats,
					    LDLM_POOL_SHRINK_REQTD_STAT,
					    nr);
			lprocfs_counter_add(pl->pl_stats,
					    LDLM_POOL_SHRINK_FREED_STAT,
					    cancel);
			CDEBUG(D_DLMTRACE, "%s: request to shrink %d locks, shrunk %d\n",
			       pl->pl_name, nr, cancel);
		}
	}
	return cancel;
}

static int lprocfs_pool_state_seq_show(struct seq_file *m, void *unused)
{
	int granted, grant_rate, cancel_rate;
	int grant_speed, lvf;
	struct ldlm_pool *pl = m->private;
	__u64 slv, clv;
	__u32 limit;

	spin_lock(&pl->pl_lock);
	slv = pl->pl_server_lock_volume;
	clv = pl->pl_client_lock_volume;
	limit = atomic_read(&pl->pl_limit);
	granted = atomic_read(&pl->pl_granted);
	grant_rate = atomic_read(&pl->pl_grant_rate);
	cancel_rate = atomic_read(&pl->pl_cancel_rate);
	grant_speed = grant_rate - cancel_rate;
	lvf = atomic_read(&pl->pl_lock_volume_factor);
	spin_unlock(&pl->pl_lock);

	seq_printf(m, "LDLM pool state (%s):\n"
		      "  SLV: %llu\n"
		      "  CLV: %llu\n"
		      "  LVF: %d\n",
		      pl->pl_name, slv, clv, lvf);

	seq_printf(m, "  GR:  %d\n  CR:  %d\n  GS:  %d\n"
		      "  G:   %d\n  L:   %d\n",
		      grant_rate, cancel_rate, grant_speed,
		      granted, limit);

	return 0;
}

LPROC_SEQ_FOPS_RO(lprocfs_pool_state);

static ssize_t grant_speed_show(struct kobject *kobj, struct attribute *attr,
				char *buf)
{
	struct ldlm_pool *pl = container_of(kobj, struct ldlm_pool,
					    pl_kobj);

	int	       grant_speed;

	spin_lock(&pl->pl_lock);
	/* serialize with ldlm_pool_recalc */
	grant_speed = atomic_read(&pl->pl_grant_rate) -
			atomic_read(&pl->pl_cancel_rate);
	spin_unlock(&pl->pl_lock);
	return sprintf(buf, "%d\n", grant_speed);
}
LUSTRE_RO_ATTR(grant_speed);

LDLM_POOL_SYSFS_READER_SHOW(grant_plan, int);
LUSTRE_RO_ATTR(grant_plan);

LDLM_POOL_SYSFS_READER_SHOW(recalc_period, int);
LDLM_POOL_SYSFS_WRITER_STORE(recalc_period, int);
LUSTRE_RW_ATTR(recalc_period);

LDLM_POOL_SYSFS_READER_NOLOCK_SHOW(server_lock_volume, u64);
LUSTRE_RO_ATTR(server_lock_volume);

LDLM_POOL_SYSFS_READER_NOLOCK_SHOW(limit, atomic);
LDLM_POOL_SYSFS_WRITER_NOLOCK_STORE(limit, atomic);
LUSTRE_RW_ATTR(limit);

LDLM_POOL_SYSFS_READER_NOLOCK_SHOW(granted, atomic);
LUSTRE_RO_ATTR(granted);

LDLM_POOL_SYSFS_READER_NOLOCK_SHOW(cancel_rate, atomic);
LUSTRE_RO_ATTR(cancel_rate);

LDLM_POOL_SYSFS_READER_NOLOCK_SHOW(grant_rate, atomic);
LUSTRE_RO_ATTR(grant_rate);

LDLM_POOL_SYSFS_READER_NOLOCK_SHOW(lock_volume_factor, atomic);
LDLM_POOL_SYSFS_WRITER_NOLOCK_STORE(lock_volume_factor, atomic);
LUSTRE_RW_ATTR(lock_volume_factor);

#define LDLM_POOL_ADD_VAR(name, var, ops)			\
	do {							\
		snprintf(var_name, MAX_STRING_SIZE, #name);	\
		pool_vars[0].data = var;			\
		pool_vars[0].fops = ops;			\
		ldebugfs_add_vars(pl->pl_debugfs_entry, pool_vars, NULL);\
	} while (0)

/* These are for pools in /sys/fs/lustre/ldlm/namespaces/.../pool */
static struct attribute *ldlm_pl_attrs[] = {
	&lustre_attr_grant_speed.attr,
	&lustre_attr_grant_plan.attr,
	&lustre_attr_recalc_period.attr,
	&lustre_attr_server_lock_volume.attr,
	&lustre_attr_limit.attr,
	&lustre_attr_granted.attr,
	&lustre_attr_cancel_rate.attr,
	&lustre_attr_grant_rate.attr,
	&lustre_attr_lock_volume_factor.attr,
	NULL,
};

static void ldlm_pl_release(struct kobject *kobj)
{
	struct ldlm_pool *pl = container_of(kobj, struct ldlm_pool,
					    pl_kobj);
	complete(&pl->pl_kobj_unregister);
}

static struct kobj_type ldlm_pl_ktype = {
	.default_attrs	= ldlm_pl_attrs,
	.sysfs_ops	= &lustre_sysfs_ops,
	.release	= ldlm_pl_release,
};

static int ldlm_pool_sysfs_init(struct ldlm_pool *pl)
{
	struct ldlm_namespace *ns = container_of(pl, struct ldlm_namespace,
						 ns_pool);
	int err;

	init_completion(&pl->pl_kobj_unregister);
	err = kobject_init_and_add(&pl->pl_kobj, &ldlm_pl_ktype, &ns->ns_kobj,
				   "pool");

	return err;
}

static int ldlm_pool_debugfs_init(struct ldlm_pool *pl)
{
	struct ldlm_namespace *ns = container_of(pl, struct ldlm_namespace,
						 ns_pool);
	struct dentry *debugfs_ns_parent;
	struct lprocfs_vars pool_vars[2];
	char *var_name = NULL;
	int rc = 0;

	var_name = kzalloc(MAX_STRING_SIZE + 1, GFP_NOFS);
	if (!var_name)
		return -ENOMEM;

	debugfs_ns_parent = ns->ns_debugfs_entry;
	if (IS_ERR_OR_NULL(debugfs_ns_parent)) {
		CERROR("%s: debugfs entry is not initialized\n",
		       ldlm_ns_name(ns));
		rc = -EINVAL;
		goto out_free_name;
	}
	pl->pl_debugfs_entry = ldebugfs_register("pool", debugfs_ns_parent,
						 NULL, NULL);
	if (IS_ERR(pl->pl_debugfs_entry)) {
		CERROR("LdebugFS failed in ldlm-pool-init\n");
		rc = PTR_ERR(pl->pl_debugfs_entry);
		pl->pl_debugfs_entry = NULL;
		goto out_free_name;
	}

	var_name[MAX_STRING_SIZE] = '\0';
	memset(pool_vars, 0, sizeof(pool_vars));
	pool_vars[0].name = var_name;

	LDLM_POOL_ADD_VAR(state, pl, &lprocfs_pool_state_fops);

	pl->pl_stats = lprocfs_alloc_stats(LDLM_POOL_LAST_STAT -
					   LDLM_POOL_FIRST_STAT, 0);
	if (!pl->pl_stats) {
		rc = -ENOMEM;
		goto out_free_name;
	}

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
	rc = ldebugfs_register_stats(pl->pl_debugfs_entry, "stats",
				     pl->pl_stats);

out_free_name:
	kfree(var_name);
	return rc;
}

static void ldlm_pool_sysfs_fini(struct ldlm_pool *pl)
{
	kobject_put(&pl->pl_kobj);
	wait_for_completion(&pl->pl_kobj_unregister);
}

static void ldlm_pool_debugfs_fini(struct ldlm_pool *pl)
{
	if (pl->pl_stats) {
		lprocfs_free_stats(&pl->pl_stats);
		pl->pl_stats = NULL;
	}
	if (pl->pl_debugfs_entry) {
		ldebugfs_remove(&pl->pl_debugfs_entry);
		pl->pl_debugfs_entry = NULL;
	}
}

int ldlm_pool_init(struct ldlm_pool *pl, struct ldlm_namespace *ns,
		   int idx, ldlm_side_t client)
{
	int rc;

	spin_lock_init(&pl->pl_lock);
	atomic_set(&pl->pl_granted, 0);
	pl->pl_recalc_time = ktime_get_seconds();
	atomic_set(&pl->pl_lock_volume_factor, 1);

	atomic_set(&pl->pl_grant_rate, 0);
	atomic_set(&pl->pl_cancel_rate, 0);
	pl->pl_grant_plan = LDLM_POOL_GP(LDLM_POOL_HOST_L);

	snprintf(pl->pl_name, sizeof(pl->pl_name), "ldlm-pool-%s-%d",
		 ldlm_ns_name(ns), idx);

	atomic_set(&pl->pl_limit, 1);
	pl->pl_server_lock_volume = 0;
	pl->pl_ops = &ldlm_cli_pool_ops;
	pl->pl_recalc_period = LDLM_POOL_CLI_DEF_RECALC_PERIOD;
	pl->pl_client_lock_volume = 0;
	rc = ldlm_pool_debugfs_init(pl);
	if (rc)
		return rc;

	rc = ldlm_pool_sysfs_init(pl);
	if (rc)
		return rc;

	CDEBUG(D_DLMTRACE, "Lock pool %s is initialized\n", pl->pl_name);

	return rc;
}
EXPORT_SYMBOL(ldlm_pool_init);

void ldlm_pool_fini(struct ldlm_pool *pl)
{
	ldlm_pool_sysfs_fini(pl);
	ldlm_pool_debugfs_fini(pl);

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

/**
 * Returns current LVF from \a pl.
 */
__u32 ldlm_pool_get_lvf(struct ldlm_pool *pl)
{
	return atomic_read(&pl->pl_lock_volume_factor);
}

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
	unsigned long total = 0;
	int nr_ns;
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

		if (!ns_old)
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
	return freed;
}

static unsigned long ldlm_pools_cli_count(struct shrinker *s,
					  struct shrink_control *sc)
{
	return ldlm_pools_count(LDLM_NAMESPACE_CLIENT, sc->gfp_mask);
}

static unsigned long ldlm_pools_cli_scan(struct shrinker *s,
					 struct shrink_control *sc)
{
	return ldlm_pools_scan(LDLM_NAMESPACE_CLIENT, sc->nr_to_scan,
			       sc->gfp_mask);
}

static int ldlm_pools_recalc(ldlm_side_t client)
{
	struct ldlm_namespace *ns;
	struct ldlm_namespace *ns_old = NULL;
	int nr;
	int time = 50; /* seconds of sleep if no active namespaces */

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

		if (!ns_old)
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

static int ldlm_pools_thread_main(void *arg)
{
	struct ptlrpc_thread *thread = (struct ptlrpc_thread *)arg;
	int c_time;

	thread_set_flags(thread, SVC_RUNNING);
	wake_up(&thread->t_ctl_waitq);

	CDEBUG(D_DLMTRACE, "%s: pool thread starting, process %d\n",
	       "ldlm_poold", current_pid());

	while (1) {
		struct l_wait_info lwi;

		/*
		 * Recal all pools on this tick.
		 */
		c_time = ldlm_pools_recalc(LDLM_NAMESPACE_CLIENT);

		/*
		 * Wait until the next check time, or until we're
		 * stopped.
		 */
		lwi = LWI_TIMEOUT(cfs_time_seconds(c_time),
				  NULL, NULL);
		l_wait_event(thread->t_ctl_waitq,
			     thread_is_stopping(thread) ||
			     thread_is_event(thread),
			     &lwi);

		if (thread_test_and_clear_flags(thread, SVC_STOPPING))
			break;
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

	if (ldlm_pools_thread)
		return -EALREADY;

	ldlm_pools_thread = kzalloc(sizeof(*ldlm_pools_thread), GFP_NOFS);
	if (!ldlm_pools_thread)
		return -ENOMEM;

	init_completion(&ldlm_pools_comp);
	init_waitqueue_head(&ldlm_pools_thread->t_ctl_waitq);

	task = kthread_run(ldlm_pools_thread_main, ldlm_pools_thread,
			   "ldlm_poold");
	if (IS_ERR(task)) {
		CERROR("Can't start pool thread, error %ld\n", PTR_ERR(task));
		kfree(ldlm_pools_thread);
		ldlm_pools_thread = NULL;
		return PTR_ERR(task);
	}
	l_wait_event(ldlm_pools_thread->t_ctl_waitq,
		     thread_is_running(ldlm_pools_thread), &lwi);
	return 0;
}

static void ldlm_pools_thread_stop(void)
{
	if (!ldlm_pools_thread)
		return;

	thread_set_flags(ldlm_pools_thread, SVC_STOPPING);
	wake_up(&ldlm_pools_thread->t_ctl_waitq);

	/*
	 * Make sure that pools thread is finished before freeing @thread.
	 * This fixes possible race and oops due to accessing freed memory
	 * in pools thread.
	 */
	wait_for_completion(&ldlm_pools_comp);
	kfree(ldlm_pools_thread);
	ldlm_pools_thread = NULL;
}

static struct shrinker ldlm_pools_cli_shrinker = {
	.count_objects	= ldlm_pools_cli_count,
	.scan_objects	= ldlm_pools_cli_scan,
	.seeks		= DEFAULT_SEEKS,
};

int ldlm_pools_init(void)
{
	int rc;

	rc = ldlm_pools_thread_start();
	if (rc == 0)
		register_shrinker(&ldlm_pools_cli_shrinker);

	return rc;
}
EXPORT_SYMBOL(ldlm_pools_init);

void ldlm_pools_fini(void)
{
	if (ldlm_pools_thread)
		unregister_shrinker(&ldlm_pools_cli_shrinker);

	ldlm_pools_thread_stop();
}
EXPORT_SYMBOL(ldlm_pools_fini);
