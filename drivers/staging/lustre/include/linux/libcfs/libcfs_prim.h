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
 * Copyright (c) 2008, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * libcfs/include/libcfs/libcfs_prim.h
 *
 * General primitives.
 *
 */

#ifndef __LIBCFS_PRIM_H__
#define __LIBCFS_PRIM_H__

#ifndef EXPORT_SYMBOL
# define EXPORT_SYMBOL(s)
#endif

/*
 * Schedule
 */
void cfs_pause(cfs_duration_t ticks);

/*
 * Timer
 */
typedef  void (cfs_timer_func_t)(ulong_ptr_t);
void schedule_timeout_and_set_state(cfs_task_state_t, int64_t);

void init_waitqueue_entry_current(wait_queue_t *link);
int64_t waitq_timedwait(wait_queue_t *, cfs_task_state_t, int64_t);
void waitq_wait(wait_queue_t *, cfs_task_state_t);
void add_wait_queue_exclusive_head(wait_queue_head_t *, wait_queue_t *);

void cfs_init_timer(timer_list_t *t);
void cfs_timer_init(timer_list_t *t, cfs_timer_func_t *func, void *arg);
void cfs_timer_done(timer_list_t *t);
void cfs_timer_arm(timer_list_t *t, cfs_time_t deadline);
void cfs_timer_disarm(timer_list_t *t);
int  cfs_timer_is_armed(timer_list_t *t);
cfs_time_t cfs_timer_deadline(timer_list_t *t);

/*
 * Memory
 */
#ifndef memory_pressure_get
#define memory_pressure_get() (0)
#endif
#ifndef memory_pressure_set
#define memory_pressure_set() do {} while (0)
#endif
#ifndef memory_pressure_clr
#define memory_pressure_clr() do {} while (0)
#endif

static inline int cfs_memory_pressure_get_and_set(void)
{
	int old = memory_pressure_get();

	if (!old)
		memory_pressure_set();
	return old;
}

static inline void cfs_memory_pressure_restore(int old)
{
	if (old)
		memory_pressure_set();
	else
		memory_pressure_clr();
	return;
}
#endif
