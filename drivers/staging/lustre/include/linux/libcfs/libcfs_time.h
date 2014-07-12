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
 * libcfs/include/libcfs/libcfs_time.h
 *
 * Time functions.
 *
 */

#ifndef __LIBCFS_TIME_H__
#define __LIBCFS_TIME_H__
/*
 * generic time manipulation functions.
 */

static inline unsigned long cfs_time_add(unsigned long t, long d)
{
	return (unsigned long)(t + d);
}

static inline unsigned long cfs_time_sub(unsigned long t1, unsigned long t2)
{
	return (unsigned long)(t1 - t2);
}

static inline int cfs_time_after(unsigned long t1, unsigned long t2)
{
	return time_before(t2, t1);
}

static inline int cfs_time_aftereq(unsigned long t1, unsigned long t2)
{
	return time_before_eq(t2, t1);
}

static inline unsigned long cfs_time_shift(int seconds)
{
	return cfs_time_add(cfs_time_current(), cfs_time_seconds(seconds));
}

static inline long cfs_timeval_sub(struct timeval *large, struct timeval *small,
				   struct timeval *result)
{
	long r = (long)(
		(large->tv_sec - small->tv_sec) * ONE_MILLION +
		(large->tv_usec - small->tv_usec));
	if (result != NULL) {
		result->tv_usec = r % ONE_MILLION;
		result->tv_sec = r / ONE_MILLION;
	}
	return r;
}

static inline void cfs_slow_warning(unsigned long now, int seconds, char *msg)
{
	if (cfs_time_after(cfs_time_current(),
			   cfs_time_add(now, cfs_time_seconds(15))))
		CERROR("slow %s "CFS_TIME_T" sec\n", msg,
		       cfs_duration_sec(cfs_time_sub(cfs_time_current(), now)));
}

#define CFS_RATELIMIT(seconds)				  \
({							      \
	/*						      \
	 * XXX nikita: non-portable initializer		 \
	 */						     \
	static time_t __next_message = 0;		       \
	int result;					     \
								\
	if (cfs_time_after(cfs_time_current(), __next_message)) \
		result = 1;				     \
	else {						  \
		__next_message = cfs_time_shift(seconds);       \
		result = 0;				     \
	}						       \
	result;						 \
})

/*
 * helper function similar to do_gettimeofday() of Linux kernel
 */
static inline void cfs_fs_timeval(struct timeval *tv)
{
	struct timespec time;

	cfs_fs_time_current(&time);
	cfs_fs_time_usec(&time, tv);
}

/*
 * return valid time-out based on user supplied one. Currently we only check
 * that time-out is not shorted than allowed.
 */
static inline long cfs_timeout_cap(long timeout)
{
	if (timeout < CFS_TICK)
		timeout = CFS_TICK;
	return timeout;
}

#endif
