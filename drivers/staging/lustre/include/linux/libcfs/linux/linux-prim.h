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
 *
 * Copyright (c) 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * libcfs/include/libcfs/linux/linux-prim.h
 *
 * Basic library routines.
 */

#ifndef __LIBCFS_LINUX_CFS_PRIM_H__
#define __LIBCFS_LINUX_CFS_PRIM_H__

#ifndef __LIBCFS_LIBCFS_H__
#error Do not #include this file directly. #include <linux/libcfs/libcfs.h> instead
#endif


#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/proc_fs.h>
#include <linux/mm.h>
#include <linux/timer.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/random.h>

#include <linux/miscdevice.h>
#include <linux/libcfs/linux/portals_compat25.h>
#include <asm/div64.h>

#include <linux/libcfs/linux/linux-time.h>


/*
 * CPU
 */
#ifdef for_each_possible_cpu
#define cfs_for_each_possible_cpu(cpu) for_each_possible_cpu(cpu)
#elif defined(for_each_cpu)
#define cfs_for_each_possible_cpu(cpu) for_each_cpu(cpu)
#endif

#ifdef NR_CPUS
#else
#define NR_CPUS     1
#endif

/*
 * cache
 */

/*
 * IRQs
 */


/*
 * Pseudo device register
 */
typedef struct miscdevice		psdev_t;

/*
 * Sysctl register
 */
typedef struct ctl_table		ctl_table_t;
typedef struct ctl_table_header		ctl_table_header_t;

#define cfs_register_sysctl_table(t, a) register_sysctl_table(t)

#define DECLARE_PROC_HANDLER(name)		      \
static int					      \
LL_PROC_PROTO(name)				     \
{						       \
	DECLARE_LL_PROC_PPOS_DECL;		      \
							\
	return proc_call_handler(table->data, write,    \
				 ppos, buffer, lenp,    \
				 __##name);	     \
}

/*
 * Symbol register
 */
#define cfs_symbol_register(s, p)       do {} while(0)
#define cfs_symbol_unregister(s)	do {} while(0)
#define cfs_symbol_get(s)	       symbol_get(s)
#define cfs_symbol_put(s)	       symbol_put(s)

typedef struct module module_t;

/*
 * Proc file system APIs
 */
typedef struct proc_dir_entry	   proc_dir_entry_t;

/*
 * Wait Queue
 */


typedef long			    cfs_task_state_t;

#define CFS_DECL_WAITQ(wq)		DECLARE_WAIT_QUEUE_HEAD(wq)

/*
 * Task struct
 */
typedef struct task_struct	      task_t;
#define DECL_JOURNAL_DATA	   void *journal_info
#define PUSH_JOURNAL		do {    \
	journal_info = current->journal_info;   \
	current->journal_info = NULL;	   \
	} while(0)
#define POP_JOURNAL		 do {    \
	current->journal_info = journal_info;   \
	} while(0)

/* Module interfaces */
#define cfs_module(name, version, init, fini) \
	module_init(init);		    \
	module_exit(fini)

/*
 * Signal
 */

/*
 * Timer
 */
typedef struct timer_list timer_list_t;


#ifndef wait_event_timeout /* Only for RHEL3 2.4.21 kernel */
#define __wait_event_timeout(wq, condition, timeout, ret)	\
do {							     \
	int __ret = 0;					   \
	if (!(condition)) {				      \
		wait_queue_t __wait;			     \
		unsigned long expire;			    \
								 \
		init_waitqueue_entry(&__wait, current);	  \
		expire = timeout + jiffies;		      \
		add_wait_queue(&wq, &__wait);		    \
		for (;;) {				       \
			set_current_state(TASK_UNINTERRUPTIBLE); \
			if (condition)			   \
				break;			   \
			if (jiffies > expire) {		  \
				ret = jiffies - expire;	  \
				break;			   \
			}					\
			schedule_timeout(timeout);	       \
		}						\
		current->state = TASK_RUNNING;		   \
		remove_wait_queue(&wq, &__wait);		 \
	}							\
} while (0)
/*
   retval == 0; condition met; we're good.
   retval > 0; timed out.
*/
#define cfs_waitq_wait_event_timeout(wq, condition, timeout, ret)    \
do {								 \
	ret = 0;						     \
	if (!(condition))					    \
		__wait_event_timeout(wq, condition, timeout, ret);   \
} while (0)
#else
#define cfs_waitq_wait_event_timeout(wq, condition, timeout, ret)    \
	ret = wait_event_timeout(wq, condition, timeout)
#endif

#define cfs_waitq_wait_event_interruptible_timeout(wq, c, timeout, ret) \
	ret = wait_event_interruptible_timeout(wq, c, timeout)

/*
 * atomic
 */


#define cfs_atomic_add_unless(atom, a, u)    atomic_add_unless(atom, a, u)
#define cfs_atomic_cmpxchg(atom, old, nv)    atomic_cmpxchg(atom, old, nv)

/*
 * membar
 */


/*
 * interrupt
 */


/*
 * might_sleep
 */

/*
 * group_info
 */
typedef struct group_info group_info_t;


/*
 * Random bytes
 */
#endif
