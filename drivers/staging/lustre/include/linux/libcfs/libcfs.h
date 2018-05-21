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
 * Copyright (c) 2011, 2015, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 */

#ifndef __LIBCFS_LIBCFS_H__
#define __LIBCFS_LIBCFS_H__

#include <linux/notifier.h>
#include <linux/workqueue.h>
#include <linux/sysctl.h>

#include <linux/libcfs/libcfs_debug.h>
#include <linux/libcfs/libcfs_private.h>
#include <linux/libcfs/libcfs_fail.h>

#define LIBCFS_VERSION "0.7.0"

extern struct blocking_notifier_head libcfs_ioctl_list;
static inline int notifier_from_ioctl_errno(int err)
{
	if (err == -EINVAL)
		return NOTIFY_OK;
	return notifier_from_errno(err) | NOTIFY_STOP_MASK;
}

int libcfs_setup(void);

extern struct workqueue_struct *cfs_rehash_wq;

void lustre_insert_debugfs(struct ctl_table *table);
int lprocfs_call_handler(void *data, int write, loff_t *ppos,
			 void __user *buffer, size_t *lenp,
			 int (*handler)(void *data, int write, loff_t pos,
					void __user *buffer, int len));

/*
 * Memory
 */
#if BITS_PER_LONG == 32
/* limit to lowmem on 32-bit systems */
#define NUM_CACHEPAGES \
	min(totalram_pages, 1UL << (30 - PAGE_SHIFT) * 3 / 4)
#else
#define NUM_CACHEPAGES totalram_pages
#endif

#endif /* __LIBCFS_LIBCFS_H__ */
