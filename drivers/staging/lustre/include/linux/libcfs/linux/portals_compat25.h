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
 */

#ifndef __LIBCFS_LINUX_PORTALS_COMPAT_H__
#define __LIBCFS_LINUX_PORTALS_COMPAT_H__

/* XXX BUG 1511 -- remove this stanza and all callers when bug 1511 is resolved */
#if defined(SPINLOCK_DEBUG) && SPINLOCK_DEBUG
#  define SIGNAL_MASK_ASSERT() \
   LASSERT(current->sighand->siglock.magic == SPINLOCK_MAGIC)
#else
# define SIGNAL_MASK_ASSERT()
#endif
/* XXX BUG 1511 -- remove this stanza and all callers when bug 1511 is resolved */

#define SIGNAL_MASK_LOCK(task, flags)				  \
	spin_lock_irqsave(&task->sighand->siglock, flags)
#define SIGNAL_MASK_UNLOCK(task, flags)				\
	spin_unlock_irqrestore(&task->sighand->siglock, flags)
#define USERMODEHELPER(path, argv, envp)			       \
	call_usermodehelper(path, argv, envp, 1)
#define clear_tsk_thread_flag(current, TIF_SIGPENDING)	  clear_tsk_thread_flag(current,       \
							TIF_SIGPENDING)
# define smp_num_cpus	      num_online_cpus()

#define cfs_wait_event_interruptible(wq, condition, ret)	       \
	ret = wait_event_interruptible(wq, condition)
#define cfs_wait_event_interruptible_exclusive(wq, condition, ret)     \
	ret = wait_event_interruptible_exclusive(wq, condition)

#define THREAD_NAME(comm, len, fmt, a...)			      \
	snprintf(comm, len, fmt, ## a)

/* 2.6 alloc_page users can use page->lru */
#define PAGE_LIST_ENTRY lru
#define PAGE_LIST(page) ((page)->lru)

#ifndef __user
#define __user
#endif

#ifndef __fls
#define __cfs_fls fls
#else
#define __cfs_fls __fls
#endif

#define ll_proc_dointvec(table, write, filp, buffer, lenp, ppos)	\
	proc_dointvec(table, write, buffer, lenp, ppos);

#define ll_proc_dolongvec(table, write, filp, buffer, lenp, ppos)	\
	proc_doulongvec_minmax(table, write, buffer, lenp, ppos);

#endif /* _PORTALS_COMPAT_H */
