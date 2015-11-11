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

#ifndef __LIBCFS_LINUX_LIBCFS_H__
#define __LIBCFS_LINUX_LIBCFS_H__

#ifndef __LIBCFS_LIBCFS_H__
#error Do not #include this file directly. #include <linux/libcfs/libcfs.h> instead
#endif

#include <linux/bitops.h>
#include <linux/compiler.h>
#include <linux/ctype.h>
#include <linux/errno.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/highmem.h>
#include <linux/interrupt.h>
#include <linux/kallsyms.h>
#include <linux/kernel.h>
#include <linux/kmod.h>
#include <linux/kthread.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <linux/mm_inline.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/mutex.h>
#include <linux/notifier.h>
#include <linux/random.h>
#include <linux/rbtree.h>
#include <linux/rwsem.h>
#include <linux/scatterlist.h>
#include <linux/sched.h>
#include <linux/signal.h>
#include <linux/slab.h>
#include <linux/smp.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/time.h>
#include <linux/timer.h>
#include <linux/types.h>
#include <linux/unistd.h>
#include <linux/vmalloc.h>
#include <net/sock.h>
#include <linux/atomic.h>
#include <asm/div64.h>
#include <linux/timex.h>
#include <linux/uaccess.h>
#include <stdarg.h>
#include "linux-cpu.h"
#include "linux-time.h"
#include "linux-mem.h"

#define LUSTRE_TRACE_SIZE (THREAD_SIZE >> 5)

#if !defined(__x86_64__)
# ifdef __ia64__
#  define CDEBUG_STACK() (THREAD_SIZE -				 \
			  ((unsigned long)__builtin_dwarf_cfa() &       \
			   (THREAD_SIZE - 1)))
# else
#  define CDEBUG_STACK() (THREAD_SIZE -				 \
			  ((unsigned long)__builtin_frame_address(0) &  \
			   (THREAD_SIZE - 1)))
# endif /* __ia64__ */

#define __CHECK_STACK(msgdata, mask, cdls)			      \
do {								    \
	if (unlikely(CDEBUG_STACK() > libcfs_stack)) {		  \
		LIBCFS_DEBUG_MSG_DATA_INIT(msgdata, D_WARNING, NULL);   \
		libcfs_stack = CDEBUG_STACK();			  \
		libcfs_debug_msg(msgdata,			       \
				 "maximum lustre stack %lu\n",	  \
				 CDEBUG_STACK());		       \
		(msgdata)->msg_mask = mask;			     \
		(msgdata)->msg_cdls = cdls;			     \
		dump_stack();					   \
	      /*panic("LBUG");*/					\
	}							       \
} while (0)
#define CFS_CHECK_STACK(msgdata, mask, cdls)  __CHECK_STACK(msgdata, mask, cdls)
#else /* __x86_64__ */
#define CFS_CHECK_STACK(msgdata, mask, cdls) do {} while (0)
#define CDEBUG_STACK() (0L)
#endif /* __x86_64__ */

/* initial pid  */
#define LUSTRE_LNET_PID	  12345

#define __current_nesting_level() (0)

/**
 * Platform specific declarations for cfs_curproc API (libcfs/curproc.h)
 *
 * Implementation is in linux-curproc.c
 */
#define CFS_CURPROC_COMM_MAX (sizeof((struct task_struct *)0)->comm)

#include <linux/capability.h>

/* long integer with size equal to pointer */
typedef unsigned long ulong_ptr_t;
typedef long long_ptr_t;

#ifndef WITH_WATCHDOG
#define WITH_WATCHDOG
#endif

#endif /* _LINUX_LIBCFS_H */
