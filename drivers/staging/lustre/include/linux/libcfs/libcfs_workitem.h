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
 * Copyright (c) 2008, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * libcfs/include/libcfs/libcfs_workitem.h
 *
 * Author: Isaac Huang  <he.h.huang@oracle.com>
 *	 Liang Zhen   <zhen.liang@sun.com>
 *
 * A workitems is deferred work with these semantics:
 * - a workitem always runs in thread context.
 * - a workitem can be concurrent with other workitems but is strictly
 *   serialized with respect to itself.
 * - no CPU affinity, a workitem does not necessarily run on the same CPU
 *   that schedules it. However, this might change in the future.
 * - if a workitem is scheduled again before it has a chance to run, it
 *   runs only once.
 * - if a workitem is scheduled while it runs, it runs again after it
 *   completes; this ensures that events occurring while other events are
 *   being processed receive due attention. This behavior also allows a
 *   workitem to reschedule itself.
 *
 * Usage notes:
 * - a workitem can sleep but it should be aware of how that sleep might
 *   affect others.
 * - a workitem runs inside a kernel thread so there's no user space to access.
 * - do not use a workitem if the scheduling latency can't be tolerated.
 *
 * When wi_action returns non-zero, it means the workitem has either been
 * freed or reused and workitem scheduler won't touch it any more.
 */

#ifndef __LIBCFS_WORKITEM_H__
#define __LIBCFS_WORKITEM_H__

struct cfs_wi_sched;

void cfs_wi_sched_destroy(struct cfs_wi_sched *sched);
int cfs_wi_sched_create(char *name, struct cfs_cpt_table *cptab, int cpt,
			int nthrs, struct cfs_wi_sched **sched_pp);

struct cfs_workitem;

typedef int (*cfs_wi_action_t) (struct cfs_workitem *);
struct cfs_workitem {
	/** chain on runq or rerunq */
	struct list_head       wi_list;
	/** working function */
	cfs_wi_action_t  wi_action;
	/** arg for working function */
	void	    *wi_data;
	/** in running */
	unsigned short   wi_running:1;
	/** scheduled */
	unsigned short   wi_scheduled:1;
};

static inline void
cfs_wi_init(struct cfs_workitem *wi, void *data, cfs_wi_action_t action)
{
	INIT_LIST_HEAD(&wi->wi_list);

	wi->wi_running   = 0;
	wi->wi_scheduled = 0;
	wi->wi_data      = data;
	wi->wi_action    = action;
}

void cfs_wi_schedule(struct cfs_wi_sched *sched, struct cfs_workitem *wi);
int  cfs_wi_deschedule(struct cfs_wi_sched *sched, struct cfs_workitem *wi);
void cfs_wi_exit(struct cfs_wi_sched *sched, struct cfs_workitem *wi);

int  cfs_wi_startup(void);
void cfs_wi_shutdown(void);

/** # workitem scheduler loops before reschedule */
#define CFS_WI_RESCHED    128

#endif /* __LIBCFS_WORKITEM_H__ */
