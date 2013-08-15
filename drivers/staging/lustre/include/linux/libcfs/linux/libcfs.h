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



#include <stdarg.h>
#include <linux/libcfs/linux/linux-cpu.h>
#include <linux/libcfs/linux/linux-time.h>
#include <linux/libcfs/linux/linux-mem.h>
#include <linux/libcfs/linux/linux-prim.h>
#include <linux/libcfs/linux/linux-lock.h>
#include <linux/libcfs/linux/linux-fs.h>
#include <linux/libcfs/linux/linux-tcpip.h>
#include <linux/libcfs/linux/linux-bitops.h>
#include <linux/libcfs/linux/linux-types.h>
#include <linux/libcfs/linux/kp30.h>

#include <asm/types.h>
#include <linux/types.h>
#include <asm/timex.h>
#include <linux/sched.h> /* THREAD_SIZE */
#include <linux/rbtree.h>

#define LUSTRE_TRACE_SIZE (THREAD_SIZE >> 5)

#if !defined(__x86_64__)
# ifdef  __ia64__
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
#define CFS_CHECK_STACK(msgdata, mask, cdls) do {} while(0)
#define CDEBUG_STACK() (0L)
#endif /* __x86_64__ */

/* initial pid  */
#define LUSTRE_LNET_PID	  12345

#define ENTRY_NESTING_SUPPORT (1)
#define ENTRY_NESTING   do {;} while (0)
#define EXIT_NESTING   do {;} while (0)
#define __current_nesting_level() (0)

/**
 * Platform specific declarations for cfs_curproc API (libcfs/curproc.h)
 *
 * Implementation is in linux-curproc.c
 */
#define CFS_CURPROC_COMM_MAX (sizeof ((struct task_struct *)0)->comm)

#include <linux/capability.h>

/* long integer with size equal to pointer */
typedef unsigned long ulong_ptr_t;
typedef long long_ptr_t;

#ifndef WITH_WATCHDOG
#define WITH_WATCHDOG
#endif




#endif /* _LINUX_LIBCFS_H */
