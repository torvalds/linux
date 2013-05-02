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
 * libcfs/include/libcfs/linux/linux-fs.h
 *
 * Basic library routines.
 */

#ifndef __LIBCFS_LINUX_CFS_FS_H__
#define __LIBCFS_LINUX_CFS_FS_H__

#ifndef __LIBCFS_LIBCFS_H__
#error Do not #include this file directly. #include <linux/libcfs/libcfs.h> instead
#endif


#include <linux/fs.h>
#include <linux/stat.h>
#include <linux/mount.h>
#include <linux/backing-dev.h>
#include <linux/posix_acl_xattr.h>

#define filp_size(f)					\
	(i_size_read((f)->f_dentry->d_inode))
#define filp_poff(f)					\
	(&(f)->f_pos)

# define do_fsync(fp, flag)				\
	((fp)->f_op->fsync(fp, 0, LLONG_MAX, flag))

#define filp_read(fp, buf, size, pos)			\
	((fp)->f_op->read((fp), (buf), (size), pos))

#define filp_write(fp, buf, size, pos)			\
	((fp)->f_op->write((fp), (buf), (size), pos))

#define filp_fsync(fp)					\
	do_fsync(fp, 1)

#define flock_type(fl)			((fl)->fl_type)
#define flock_set_type(fl, type)	do { (fl)->fl_type = (type); } while (0)
#define flock_pid(fl)			((fl)->fl_pid)
#define flock_set_pid(fl, pid)		do { (fl)->fl_pid = (pid); } while (0)
#define flock_start(fl)			((fl)->fl_start)
#define flock_set_start(fl, st)		do { (fl)->fl_start = (st); } while (0)
#define flock_end(fl)			((fl)->fl_end)
#define flock_set_end(fl, end)		do { (fl)->fl_end = (end); } while (0)

ssize_t filp_user_write(struct file *filp, const void *buf, size_t count,
			loff_t *offset);

#ifndef IFSHIFT
#define IFSHIFT			12
#endif

#ifndef IFTODT
#define IFTODT(type)		(((type) & S_IFMT) >> IFSHIFT)
#endif
#ifndef DTTOIF
#define DTTOIF(dirtype)		((dirtype) << IFSHIFT)
#endif

#endif
