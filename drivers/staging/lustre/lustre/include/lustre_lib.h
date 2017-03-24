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
 * lustre/include/lustre_lib.h
 *
 * Basic Lustre library routines.
 */

#ifndef _LUSTRE_LIB_H
#define _LUSTRE_LIB_H

/** \defgroup lib lib
 *
 * @{
 */

#include <linux/sched/signal.h>
#include <linux/signal.h>
#include <linux/types.h>
#include "../../include/linux/libcfs/libcfs.h"
#include "lustre/lustre_idl.h"
#include "lustre_ver.h"
#include "lustre_cfg.h"

/* target.c */
struct ptlrpc_request;
struct obd_export;
struct lu_target;
struct l_wait_info;
#include "lustre_ha.h"
#include "lustre_net.h"

#define LI_POISON 0x5a5a5a5a
#if BITS_PER_LONG > 32
# define LL_POISON 0x5a5a5a5a5a5a5a5aL
#else
# define LL_POISON 0x5a5a5a5aL
#endif
#define LP_POISON ((void *)LL_POISON)

int target_pack_pool_reply(struct ptlrpc_request *req);
int do_set_info_async(struct obd_import *imp,
		      int opcode, int version,
		      u32 keylen, void *key,
		      u32 vallen, void *val,
		      struct ptlrpc_request_set *set);

void target_send_reply(struct ptlrpc_request *req, int rc, int fail_id);

/*
 * l_wait_event is a flexible sleeping function, permitting simple caller
 * configuration of interrupt and timeout sensitivity along with actions to
 * be performed in the event of either exception.
 *
 * The first form of usage looks like this:
 *
 * struct l_wait_info lwi = LWI_TIMEOUT_INTR(timeout, timeout_handler,
 *					   intr_handler, callback_data);
 * rc = l_wait_event(waitq, condition, &lwi);
 *
 * l_wait_event() makes the current process wait on 'waitq' until 'condition'
 * is TRUE or a "killable" signal (SIGTERM, SIKGILL, SIGINT) is pending.  It
 * returns 0 to signify 'condition' is TRUE, but if a signal wakes it before
 * 'condition' becomes true, it optionally calls the specified 'intr_handler'
 * if not NULL, and returns -EINTR.
 *
 * If a non-zero timeout is specified, signals are ignored until the timeout
 * has expired.  At this time, if 'timeout_handler' is not NULL it is called.
 * If it returns FALSE l_wait_event() continues to wait as described above with
 * signals enabled.  Otherwise it returns -ETIMEDOUT.
 *
 * LWI_INTR(intr_handler, callback_data) is shorthand for
 * LWI_TIMEOUT_INTR(0, NULL, intr_handler, callback_data)
 *
 * The second form of usage looks like this:
 *
 * struct l_wait_info lwi = LWI_TIMEOUT(timeout, timeout_handler);
 * rc = l_wait_event(waitq, condition, &lwi);
 *
 * This form is the same as the first except that it COMPLETELY IGNORES
 * SIGNALS.  The caller must therefore beware that if 'timeout' is zero, or if
 * 'timeout_handler' is not NULL and returns FALSE, then the ONLY thing that
 * can unblock the current process is 'condition' becoming TRUE.
 *
 * Another form of usage is:
 * struct l_wait_info lwi = LWI_TIMEOUT_INTERVAL(timeout, interval,
 *					       timeout_handler);
 * rc = l_wait_event(waitq, condition, &lwi);
 * This is the same as previous case, but condition is checked once every
 * 'interval' jiffies (if non-zero).
 *
 * Subtle synchronization point: this macro does *not* necessary takes
 * wait-queue spin-lock before returning, and, hence, following idiom is safe
 * ONLY when caller provides some external locking:
 *
 *	     Thread1			    Thread2
 *
 *   l_wait_event(&obj->wq, ....);				       (1)
 *
 *				    wake_up(&obj->wq):		 (2)
 *					 spin_lock(&q->lock);	  (2.1)
 *					 __wake_up_common(q, ...);     (2.2)
 *					 spin_unlock(&q->lock, flags); (2.3)
 *
 *   kfree(obj);						  (3)
 *
 * As l_wait_event() may "short-cut" execution and return without taking
 * wait-queue spin-lock, some additional synchronization is necessary to
 * guarantee that step (3) can begin only after (2.3) finishes.
 *
 * XXX nikita: some ptlrpc daemon threads have races of that sort.
 *
 */
static inline int back_to_sleep(void *arg)
{
	return 0;
}

#define LWI_ON_SIGNAL_NOOP ((void (*)(void *))(-1))

struct l_wait_info {
	long lwi_timeout;
	long lwi_interval;
	int	    lwi_allow_intr;
	int  (*lwi_on_timeout)(void *);
	void (*lwi_on_signal)(void *);
	void  *lwi_cb_data;
};

/* NB: LWI_TIMEOUT ignores signals completely */
#define LWI_TIMEOUT(time, cb, data)	     \
((struct l_wait_info) {			 \
	.lwi_timeout    = time,		 \
	.lwi_on_timeout = cb,		   \
	.lwi_cb_data    = data,		 \
	.lwi_interval   = 0,		    \
	.lwi_allow_intr = 0		     \
})

#define LWI_TIMEOUT_INTERVAL(time, interval, cb, data)  \
((struct l_wait_info) {				 \
	.lwi_timeout    = time,			 \
	.lwi_on_timeout = cb,			   \
	.lwi_cb_data    = data,			 \
	.lwi_interval   = interval,		     \
	.lwi_allow_intr = 0			     \
})

#define LWI_TIMEOUT_INTR(time, time_cb, sig_cb, data)   \
((struct l_wait_info) {				 \
	.lwi_timeout    = time,			 \
	.lwi_on_timeout = time_cb,		      \
	.lwi_on_signal  = sig_cb,		       \
	.lwi_cb_data    = data,			 \
	.lwi_interval   = 0,			    \
	.lwi_allow_intr = 0			     \
})

#define LWI_TIMEOUT_INTR_ALL(time, time_cb, sig_cb, data)       \
((struct l_wait_info) {					 \
	.lwi_timeout    = time,				 \
	.lwi_on_timeout = time_cb,			      \
	.lwi_on_signal  = sig_cb,			       \
	.lwi_cb_data    = data,				 \
	.lwi_interval   = 0,				    \
	.lwi_allow_intr = 1				     \
})

#define LWI_INTR(cb, data)  LWI_TIMEOUT_INTR(0, NULL, cb, data)

#define LUSTRE_FATAL_SIGS (sigmask(SIGKILL) | sigmask(SIGINT) |		\
			   sigmask(SIGTERM) | sigmask(SIGQUIT) |	\
			   sigmask(SIGALRM))

/**
 * wait_queue_t of Linux (version < 2.6.34) is a FIFO list for exclusively
 * waiting threads, which is not always desirable because all threads will
 * be waken up again and again, even user only needs a few of them to be
 * active most time. This is not good for performance because cache can
 * be polluted by different threads.
 *
 * LIFO list can resolve this problem because we always wakeup the most
 * recent active thread by default.
 *
 * NB: please don't call non-exclusive & exclusive wait on the same
 * waitq if add_wait_queue_exclusive_head is used.
 */
#define add_wait_queue_exclusive_head(waitq, link)		\
{								\
	unsigned long flags;					\
								\
	spin_lock_irqsave(&((waitq)->lock), flags);		\
	__add_wait_queue_exclusive(waitq, link);		\
	spin_unlock_irqrestore(&((waitq)->lock), flags);	\
}

/*
 * wait for @condition to become true, but no longer than timeout, specified
 * by @info.
 */
#define __l_wait_event(wq, condition, info, ret, l_add_wait)		   \
do {									   \
	wait_queue_t __wait;						 \
	long __timeout = info->lwi_timeout;			  \
	sigset_t   __blocked;					      \
	int   __allow_intr = info->lwi_allow_intr;			     \
									       \
	ret = 0;							       \
	if (condition)							 \
		break;							 \
									       \
	init_waitqueue_entry(&__wait, current);					    \
	l_add_wait(&wq, &__wait);					      \
									       \
	/* Block all signals (just the non-fatal ones if no timeout). */       \
	if (info->lwi_on_signal && (__timeout == 0 || __allow_intr))   \
		__blocked = cfs_block_sigsinv(LUSTRE_FATAL_SIGS);	      \
	else								   \
		__blocked = cfs_block_sigsinv(0);			      \
									       \
	for (;;) {							     \
		if (condition)						 \
			break;						 \
									       \
		set_current_state(TASK_INTERRUPTIBLE);			       \
									       \
		if (__timeout == 0) {					  \
			schedule();					       \
		} else {						       \
			long interval = info->lwi_interval ?	  \
					     min_t(long,	     \
						 info->lwi_interval, __timeout) : \
					     __timeout;			\
			long remaining = schedule_timeout(interval);\
			__timeout = cfs_time_sub(__timeout,		    \
					    cfs_time_sub(interval, remaining));\
			if (__timeout == 0) {				  \
				if (!info->lwi_on_timeout ||		      \
				    info->lwi_on_timeout(info->lwi_cb_data)) { \
					ret = -ETIMEDOUT;		      \
					break;				 \
				}					      \
				/* Take signals after the timeout expires. */  \
				if (info->lwi_on_signal)		       \
				    (void)cfs_block_sigsinv(LUSTRE_FATAL_SIGS);\
			}						      \
		}							      \
									       \
		set_current_state(TASK_RUNNING);			       \
									       \
		if (condition)						 \
			break;						 \
		if (signal_pending(current)) {				    \
			if (info->lwi_on_signal &&		     \
			    (__timeout == 0 || __allow_intr)) {		\
				if (info->lwi_on_signal != LWI_ON_SIGNAL_NOOP) \
					info->lwi_on_signal(info->lwi_cb_data);\
				ret = -EINTR;				  \
				break;					 \
			}						      \
			/* We have to do this here because some signals */     \
			/* are not blockable - ie from strace(1).       */     \
			/* In these cases we want to schedule_timeout() */     \
			/* again, because we don't want that to return  */     \
			/* -EINTR when the RPC actually succeeded.      */     \
			/* the recalc_sigpending() below will deliver the */     \
			/* signal properly.			     */     \
			cfs_clear_sigpending();				\
		}							      \
	}								      \
									       \
	cfs_restore_sigs(__blocked);					   \
									       \
	remove_wait_queue(&wq, &__wait);					   \
} while (0)

#define l_wait_event(wq, condition, info)		       \
({							      \
	int		 __ret;			      \
	struct l_wait_info *__info = (info);		    \
								\
	__l_wait_event(wq, condition, __info,		   \
		       __ret, add_wait_queue);		   \
	__ret;						  \
})

#define l_wait_event_exclusive(wq, condition, info)	     \
({							      \
	int		 __ret;			      \
	struct l_wait_info *__info = (info);		    \
								\
	__l_wait_event(wq, condition, __info,		   \
		       __ret, add_wait_queue_exclusive);	 \
	__ret;						  \
})

#define l_wait_event_exclusive_head(wq, condition, info)	\
({							      \
	int		 __ret;			      \
	struct l_wait_info *__info = (info);		    \
								\
	__l_wait_event(wq, condition, __info,		   \
		       __ret, add_wait_queue_exclusive_head);    \
	__ret;						  \
})

#define l_wait_condition(wq, condition)			 \
({							      \
	struct l_wait_info lwi = { 0 };			 \
	l_wait_event(wq, condition, &lwi);		      \
})

#define l_wait_condition_exclusive(wq, condition)	       \
({							      \
	struct l_wait_info lwi = { 0 };			 \
	l_wait_event_exclusive(wq, condition, &lwi);	    \
})

#define l_wait_condition_exclusive_head(wq, condition)	  \
({							      \
	struct l_wait_info lwi = { 0 };			 \
	l_wait_event_exclusive_head(wq, condition, &lwi);       \
})

/** @} lib */

#endif /* _LUSTRE_LIB_H */
