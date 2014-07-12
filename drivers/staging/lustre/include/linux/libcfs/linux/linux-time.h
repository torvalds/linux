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
 * libcfs/include/libcfs/linux/linux-time.h
 *
 * Implementation of portable time API for Linux (kernel and user-level).
 *
 * Author: Nikita Danilov <nikita@clusterfs.com>
 */

#ifndef __LIBCFS_LINUX_LINUX_TIME_H__
#define __LIBCFS_LINUX_LINUX_TIME_H__

#ifndef __LIBCFS_LIBCFS_H__
#error Do not #include this file directly. #include <linux/libcfs/libcfs.h> instead
#endif

/* Portable time API */

/*
 * Platform provides three opaque data-types:
 *
 *  unsigned long	represents point in time. This is internal kernel
 *		    time rather than "wall clock". This time bears no
 *		    relation to gettimeofday().
 *
 *  cfs_duration_t    represents time interval with resolution of internal
 *		    platform clock
 *
 *  struct timespec     represents instance in world-visible time. This is
 *		    used in file-system time-stamps
 *
 *  unsigned long     cfs_time_current(void);
 *  unsigned long     cfs_time_add    (unsigned long, cfs_duration_t);
 *  cfs_duration_t cfs_time_sub    (unsigned long, unsigned long);
 *  int	    cfs_impl_time_before (unsigned long, unsigned long);
 *  int	    cfs_impl_time_before_eq(unsigned long, unsigned long);
 *
 *  cfs_duration_t cfs_duration_build(int64_t);
 *
 *  time_t	 cfs_duration_sec (cfs_duration_t);
 *  void	   cfs_duration_usec(cfs_duration_t, struct timeval *);
 *
 *  void	   cfs_fs_time_current(struct timespec *);
 *  time_t	 cfs_fs_time_sec    (struct timespec *);
 *  void	   cfs_fs_time_usec   (struct timespec *, struct timeval *);
 *
 *  CFS_TIME_FORMAT
 *  CFS_DURATION_FORMAT
 *
 */

#define ONE_BILLION ((u_int64_t)1000000000)
#define ONE_MILLION 1000000

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/time.h>
#include <asm/div64.h>

#include "portals_compat25.h"

/*
 * post 2.5 kernels.
 */

#include <linux/jiffies.h>


static inline void cfs_fs_time_usec(struct timespec *t, struct timeval *v)
{
	v->tv_sec  = t->tv_sec;
	v->tv_usec = t->tv_nsec / 1000;
}

/*
 * Generic kernel stuff
 */

typedef long cfs_duration_t;

static inline int cfs_time_before(unsigned long t1, unsigned long t2)
{
	return time_before(t1, t2);
}

static inline unsigned long cfs_time_current(void)
{
	return jiffies;
}

static inline time_t cfs_time_current_sec(void)
{
	return get_seconds();
}

static inline void cfs_fs_time_current(struct timespec *t)
{
	*t = CURRENT_TIME;
}

static inline time_t cfs_fs_time_sec(struct timespec *t)
{
	return t->tv_sec;
}

static inline cfs_duration_t cfs_time_seconds(int seconds)
{
	return ((cfs_duration_t)seconds) * HZ;
}

static inline time_t cfs_duration_sec(cfs_duration_t d)
{
	return d / HZ;
}

static inline void cfs_duration_usec(cfs_duration_t d, struct timeval *s)
{
#if (BITS_PER_LONG == 32) && (HZ > 4096)
	__u64 t;

	s->tv_sec = d / HZ;
	t = (d - (cfs_duration_t)s->tv_sec * HZ) * ONE_MILLION;
	do_div(t, HZ);
	s->tv_usec = t;
#else
	s->tv_sec = d / HZ;
	s->tv_usec = ((d - (cfs_duration_t)s->tv_sec * HZ) * \
		ONE_MILLION) / HZ;
#endif
}

#define cfs_time_current_64 get_jiffies_64

static inline __u64 cfs_time_add_64(__u64 t, __u64 d)
{
	return t + d;
}

static inline __u64 cfs_time_shift_64(int seconds)
{
	return cfs_time_add_64(cfs_time_current_64(),
			       cfs_time_seconds(seconds));
}

static inline int cfs_time_before_64(__u64 t1, __u64 t2)
{
	return (__s64)t2 - (__s64)t1 > 0;
}

static inline int cfs_time_beforeq_64(__u64 t1, __u64 t2)
{
	return (__s64)t2 - (__s64)t1 >= 0;
}

/*
 * One jiffy
 */
#define CFS_TICK		(1)

#define CFS_TIME_T	      "%lu"
#define CFS_DURATION_T	  "%ld"

#endif /* __LIBCFS_LINUX_LINUX_TIME_H__ */
