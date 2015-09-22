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
 * Copyright (c) 2002, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 */

#ifndef _LINUX_LL_H
#define _LINUX_LL_H

#ifndef _LL_H
#error Do not #include this file directly. #include <lustre_lite.h> instead
#endif


#include <asm/statfs.h>

#include <linux/fs.h>
#include <linux/dcache.h>

#include "../obd_class.h"
#include "../lustre_net.h"
#include "../lustre_ha.h"

#include <linux/rbtree.h>
#include "../../include/linux/lustre_compat25.h"
#include <linux/pagemap.h>

/* lprocfs.c */
enum {
	 LPROC_LL_DIRTY_HITS = 0,
	 LPROC_LL_DIRTY_MISSES,
	 LPROC_LL_READ_BYTES,
	 LPROC_LL_WRITE_BYTES,
	 LPROC_LL_BRW_READ,
	 LPROC_LL_BRW_WRITE,
	 LPROC_LL_OSC_READ,
	 LPROC_LL_OSC_WRITE,
	 LPROC_LL_IOCTL,
	 LPROC_LL_OPEN,
	 LPROC_LL_RELEASE,
	 LPROC_LL_MAP,
	 LPROC_LL_LLSEEK,
	 LPROC_LL_FSYNC,
	 LPROC_LL_READDIR,
	 LPROC_LL_SETATTR,
	 LPROC_LL_TRUNC,
	 LPROC_LL_FLOCK,
	 LPROC_LL_GETATTR,
	 LPROC_LL_CREATE,
	 LPROC_LL_LINK,
	 LPROC_LL_UNLINK,
	 LPROC_LL_SYMLINK,
	 LPROC_LL_MKDIR,
	 LPROC_LL_RMDIR,
	 LPROC_LL_MKNOD,
	 LPROC_LL_RENAME,
	 LPROC_LL_STAFS,
	 LPROC_LL_ALLOC_INODE,
	 LPROC_LL_SETXATTR,
	 LPROC_LL_GETXATTR,
	 LPROC_LL_GETXATTR_HITS,
	 LPROC_LL_LISTXATTR,
	 LPROC_LL_REMOVEXATTR,
	 LPROC_LL_INODE_PERM,
	 LPROC_LL_FILE_OPCODES
};


#endif
