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

#define ONE_BILLION ((u_int64_t)1000000000)
#define ONE_MILLION 1000000

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/time.h>
#include <asm/div64.h>

/*
 * post 2.5 kernels.
 */

#include <linux/jiffies.h>

/*
 * Generic kernel stuff
 */

static inline unsigned long cfs_time_current(void)
{
	return jiffies;
}

static inline long cfs_time_seconds(int seconds)
{
	return ((long)seconds) * msecs_to_jiffies(MSEC_PER_SEC);
}

static inline long cfs_duration_sec(long d)
{
	return d / msecs_to_jiffies(MSEC_PER_SEC);
}

#define cfs_time_current_64 get_jiffies_64

static inline u64 cfs_time_add_64(u64 t, u64 d)
{
	return t + d;
}

static inline u64 cfs_time_shift_64(int seconds)
{
	return cfs_time_add_64(cfs_time_current_64(),
			       cfs_time_seconds(seconds));
}

static inline int cfs_time_before_64(u64 t1, u64 t2)
{
	return (__s64)t2 - (__s64)t1 > 0;
}

static inline int cfs_time_beforeq_64(u64 t1, u64 t2)
{
	return (__s64)t2 - (__s64)t1 >= 0;
}

/*
 * One jiffy
 */
#define CFS_TICK		(1)

#define CFS_DURATION_T	  "%ld"

#endif /* __LIBCFS_LINUX_LINUX_TIME_H__ */
