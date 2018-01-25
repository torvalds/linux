// SPDX-License-Identifier: GPL-2.0
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
 * Copyright (c) 2011, 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * libcfs/libcfs/workitem.c
 *
 * Author: Isaac Huang <isaac@clusterfs.com>
 *	 Liang Zhen  <zhen.liang@sun.com>
 */

#define DEBUG_SUBSYSTEM S_LNET

#include <linux/libcfs/libcfs.h>

#define CFS_WS_NAME_LEN	 16

struct cfs_wi_sched {
	/* chain on global list */
	struct list_head		ws_list;
	/** serialised workitems */
	spinlock_t			ws_lock;
	/** where schedulers sleep */
	wait_queue_head_t		ws_waitq;
	/** concurrent workitems */
	struct list_head		ws_runq;
	/**
	 * rescheduled running-workitems, a workitem can be rescheduled
	 * while running in wi_action(), but we don't to execute it again
	 * unless it returns from wi_action(), so we put it on ws_rerunq
	 * while rescheduling, and move it to runq after it returns
	 * from wi_action()
	 */
	struct list_head		ws_rerunq;
	/** CPT-table for this scheduler */
	struct cfs_cpt_table		*ws_cptab;
	/** CPT id for affinity */
	int				ws_cpt;
	/** number of scheduled workitems */
	int				ws_nscheduled;
	/** started scheduler thread, protected by cfs_wi_data::wi_glock */
	unsigned int			ws_nthreads:30;
	/** shutting down, protected by cfs_wi_data::wi_glock */
	unsigned int			ws_stopping:1;
	/** serialize starting thread, protected by cfs_wi_data::wi_glock */
	unsigned int			ws_starting:1;
	/** scheduler name */
	char				ws_name[CFS_WS_NAME_LEN];
};

static struct cfs_workitem_data {
	/** serialize */
	spinlock_t		wi_glock;
	/** list of all schedulers */
	struct list_head	wi_scheds;
	/** WI module is initialized */
	int			wi_init;
	/** shutting down the whole WI module */
	int			wi_stopping;
} cfs_wi_data;

static inline int
cfs_wi_sched_cansleep(struct cfs_wi_sched *sched)
{
	spin_lock(&sched->ws_lock);
	if (sched->ws_stopping) {
		spin_unlock(&sched->ws_lock);
		return 0;
	}

	if (!list_empty(&sched->ws_runq)) {
		spin_unlock(&sched->ws_lock);
		return 0;
	}
	spin_unlock(&sched->ws_lock);
	return 1;
}

/* XXX:
 * 0. it only works when called from wi->wi_action.
 * 1. when it returns no one shall try to schedule the workitem.
 */
void
cfs_wi_exit(struct cfs_wi_sched *sched, struct cfs_workitem *wi)
{
	LASSERT(!in_interrupt()); /* because we use plain spinlock */
	LASSERT(!sched->ws_stopping);

	spin_lock(&sched->ws_lock);

	LASSERT(wi->wi_running);
	if (wi->wi_scheduled) { /* cancel pending schedules */
		LASSERT(!list_empty(&wi->wi_list));
		list_del_init(&wi->wi_list);

		LASSERT(sched->ws_nscheduled > 0);
		sched->ws_nscheduled--;
	}

	LASSERT(list_empty(&wi->wi_list));

	wi->wi_scheduled = 1; /* LBUG future schedule attempts */
	spin_unlock(&sched->ws_lock);
}
EXPORT_SYMBOL(cfs_wi_exit);

/**
 * cancel schedule request of workitem \a wi
 */
int
cfs_wi_deschedule(struct cfs_wi_sched *sched, struct cfs_workitem *wi)
{
	int rc;

	LASSERT(!in_interrupt()); /* because we use plain spinlock */
	LASSERT(!sched->ws_stopping);

	/*
	 * return 0 if it's running already, otherwise return 1, which
	 * means the workitem will not be scheduled and will not have
	 * any race with wi_action.
	 */
	spin_lock(&sched->ws_lock);

	rc = !(wi->wi_running);

	if (wi->wi_scheduled) { /* cancel pending schedules */
		LASSERT(!list_empty(&wi->wi_list));
		list_del_init(&wi->wi_list);

		LASSERT(sched->ws_nscheduled > 0);
		sched->ws_nscheduled--;

		wi->wi_scheduled = 0;
	}

	LASSERT(list_empty(&wi->wi_list));

	spin_unlock(&sched->ws_lock);
	return rc;
}
EXPORT_SYMBOL(cfs_wi_deschedule);

/*
 * Workitem scheduled with (serial == 1) is strictly serialised not only with
 * itself, but also with others scheduled this way.
 *
 * Now there's only one static serialised queue, but in the future more might
 * be added, and even dynamic creation of serialised queues might be supported.
 */
void
cfs_wi_schedule(struct cfs_wi_sched *sched, struct cfs_workitem *wi)
{
	LASSERT(!in_interrupt()); /* because we use plain spinlock */
	LASSERT(!sched->ws_stopping);

	spin_lock(&sched->ws_lock);

	if (!wi->wi_scheduled) {
		LASSERT(list_empty(&wi->wi_list));

		wi->wi_scheduled = 1;
		sched->ws_nscheduled++;
		if (!wi->wi_running) {
			list_add_tail(&wi->wi_list, &sched->ws_runq);
			wake_up(&sched->ws_waitq);
		} else {
			list_add(&wi->wi_list, &sched->ws_rerunq);
		}
	}

	LASSERT(!list_empty(&wi->wi_list));
	spin_unlock(&sched->ws_lock);
}
EXPORT_SYMBOL(cfs_wi_schedule);

static int cfs_wi_scheduler(void *arg)
{
	struct cfs_wi_sched *sched = (struct cfs_wi_sched *)arg;

	cfs_block_allsigs();

	/* CPT affinity scheduler? */
	if (sched->ws_cptab)
		if (cfs_cpt_bind(sched->ws_cptab, sched->ws_cpt))
			CWARN("Unable to bind %s on CPU partition %d\n",
			      sched->ws_name, sched->ws_cpt);

	spin_lock(&cfs_wi_data.wi_glock);

	LASSERT(sched->ws_starting == 1);
	sched->ws_starting--;
	sched->ws_nthreads++;

	spin_unlock(&cfs_wi_data.wi_glock);

	spin_lock(&sched->ws_lock);

	while (!sched->ws_stopping) {
		int nloops = 0;
		int rc;
		struct cfs_workitem *wi;

		while (!list_empty(&sched->ws_runq) &&
		       nloops < CFS_WI_RESCHED) {
			wi = list_entry(sched->ws_runq.next,
					struct cfs_workitem, wi_list);
			LASSERT(wi->wi_scheduled && !wi->wi_running);

			list_del_init(&wi->wi_list);

			LASSERT(sched->ws_nscheduled > 0);
			sched->ws_nscheduled--;

			wi->wi_running = 1;
			wi->wi_scheduled = 0;

			spin_unlock(&sched->ws_lock);
			nloops++;

			rc = (*wi->wi_action)(wi);

			spin_lock(&sched->ws_lock);
			if (rc) /* WI should be dead, even be freed! */
				continue;

			wi->wi_running = 0;
			if (list_empty(&wi->wi_list))
				continue;

			LASSERT(wi->wi_scheduled);
			/* wi is rescheduled, should be on rerunq now, we
			 * move it to runq so it can run action now
			 */
			list_move_tail(&wi->wi_list, &sched->ws_runq);
		}

		if (!list_empty(&sched->ws_runq)) {
			spin_unlock(&sched->ws_lock);
			/* don't sleep because some workitems still
			 * expect me to come back soon
			 */
			cond_resched();
			spin_lock(&sched->ws_lock);
			continue;
		}

		spin_unlock(&sched->ws_lock);
		rc = wait_event_interruptible_exclusive(sched->ws_waitq,
							!cfs_wi_sched_cansleep(sched));
		spin_lock(&sched->ws_lock);
	}

	spin_unlock(&sched->ws_lock);

	spin_lock(&cfs_wi_data.wi_glock);
	sched->ws_nthreads--;
	spin_unlock(&cfs_wi_data.wi_glock);

	return 0;
}

void
cfs_wi_sched_destroy(struct cfs_wi_sched *sched)
{
	int i;

	LASSERT(cfs_wi_data.wi_init);
	LASSERT(!cfs_wi_data.wi_stopping);

	spin_lock(&cfs_wi_data.wi_glock);
	if (sched->ws_stopping) {
		CDEBUG(D_INFO, "%s is in progress of stopping\n",
		       sched->ws_name);
		spin_unlock(&cfs_wi_data.wi_glock);
		return;
	}

	LASSERT(!list_empty(&sched->ws_list));
	sched->ws_stopping = 1;

	spin_unlock(&cfs_wi_data.wi_glock);

	i = 2;
	wake_up_all(&sched->ws_waitq);

	spin_lock(&cfs_wi_data.wi_glock);
	while (sched->ws_nthreads > 0) {
		CDEBUG(is_power_of_2(++i) ? D_WARNING : D_NET,
		       "waiting for %d threads of WI sched[%s] to terminate\n",
		       sched->ws_nthreads, sched->ws_name);

		spin_unlock(&cfs_wi_data.wi_glock);
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(cfs_time_seconds(1) / 20);
		spin_lock(&cfs_wi_data.wi_glock);
	}

	list_del(&sched->ws_list);

	spin_unlock(&cfs_wi_data.wi_glock);
	LASSERT(!sched->ws_nscheduled);

	LIBCFS_FREE(sched, sizeof(*sched));
}
EXPORT_SYMBOL(cfs_wi_sched_destroy);

int
cfs_wi_sched_create(char *name, struct cfs_cpt_table *cptab,
		    int cpt, int nthrs, struct cfs_wi_sched **sched_pp)
{
	struct cfs_wi_sched *sched;
	int rc;

	LASSERT(cfs_wi_data.wi_init);
	LASSERT(!cfs_wi_data.wi_stopping);
	LASSERT(!cptab || cpt == CFS_CPT_ANY ||
		(cpt >= 0 && cpt < cfs_cpt_number(cptab)));

	LIBCFS_ALLOC(sched, sizeof(*sched));
	if (!sched)
		return -ENOMEM;

	if (strlen(name) > sizeof(sched->ws_name) - 1) {
		LIBCFS_FREE(sched, sizeof(*sched));
		return -E2BIG;
	}
	strncpy(sched->ws_name, name, sizeof(sched->ws_name));

	sched->ws_cptab = cptab;
	sched->ws_cpt = cpt;

	spin_lock_init(&sched->ws_lock);
	init_waitqueue_head(&sched->ws_waitq);
	INIT_LIST_HEAD(&sched->ws_runq);
	INIT_LIST_HEAD(&sched->ws_rerunq);
	INIT_LIST_HEAD(&sched->ws_list);

	rc = 0;
	while (nthrs > 0)  {
		char name[16];
		struct task_struct *task;

		spin_lock(&cfs_wi_data.wi_glock);
		while (sched->ws_starting > 0) {
			spin_unlock(&cfs_wi_data.wi_glock);
			schedule();
			spin_lock(&cfs_wi_data.wi_glock);
		}

		sched->ws_starting++;
		spin_unlock(&cfs_wi_data.wi_glock);

		if (sched->ws_cptab && sched->ws_cpt >= 0) {
			snprintf(name, sizeof(name), "%s_%02d_%02u",
				 sched->ws_name, sched->ws_cpt,
				 sched->ws_nthreads);
		} else {
			snprintf(name, sizeof(name), "%s_%02u",
				 sched->ws_name, sched->ws_nthreads);
		}

		task = kthread_run(cfs_wi_scheduler, sched, "%s", name);
		if (!IS_ERR(task)) {
			nthrs--;
			continue;
		}
		rc = PTR_ERR(task);

		CERROR("Failed to create thread for WI scheduler %s: %d\n",
		       name, rc);

		spin_lock(&cfs_wi_data.wi_glock);

		/* make up for cfs_wi_sched_destroy */
		list_add(&sched->ws_list, &cfs_wi_data.wi_scheds);
		sched->ws_starting--;

		spin_unlock(&cfs_wi_data.wi_glock);

		cfs_wi_sched_destroy(sched);
		return rc;
	}
	spin_lock(&cfs_wi_data.wi_glock);
	list_add(&sched->ws_list, &cfs_wi_data.wi_scheds);
	spin_unlock(&cfs_wi_data.wi_glock);

	*sched_pp = sched;
	return 0;
}
EXPORT_SYMBOL(cfs_wi_sched_create);

int
cfs_wi_startup(void)
{
	memset(&cfs_wi_data, 0, sizeof(cfs_wi_data));

	spin_lock_init(&cfs_wi_data.wi_glock);
	INIT_LIST_HEAD(&cfs_wi_data.wi_scheds);
	cfs_wi_data.wi_init = 1;

	return 0;
}

void
cfs_wi_shutdown(void)
{
	struct cfs_wi_sched *sched;
	struct cfs_wi_sched *temp;

	spin_lock(&cfs_wi_data.wi_glock);
	cfs_wi_data.wi_stopping = 1;
	spin_unlock(&cfs_wi_data.wi_glock);

	/* nobody should contend on this list */
	list_for_each_entry(sched, &cfs_wi_data.wi_scheds, ws_list) {
		sched->ws_stopping = 1;
		wake_up_all(&sched->ws_waitq);
	}

	list_for_each_entry(sched, &cfs_wi_data.wi_scheds, ws_list) {
		spin_lock(&cfs_wi_data.wi_glock);

		while (sched->ws_nthreads) {
			spin_unlock(&cfs_wi_data.wi_glock);
			set_current_state(TASK_UNINTERRUPTIBLE);
			schedule_timeout(cfs_time_seconds(1) / 20);
			spin_lock(&cfs_wi_data.wi_glock);
		}
		spin_unlock(&cfs_wi_data.wi_glock);
	}
	list_for_each_entry_safe(sched, temp, &cfs_wi_data.wi_scheds, ws_list) {
		list_del(&sched->ws_list);
		LIBCFS_FREE(sched, sizeof(*sched));
	}

	cfs_wi_data.wi_stopping = 0;
	cfs_wi_data.wi_init = 0;
}
