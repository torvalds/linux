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
#include <linux/mm.h>
#include <linux/mm_inline.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/mutex.h>
#include <linux/notifier.h>
#include <linux/pagemap.h>
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

#endif /* _LINUX_LIBCFS_H */
