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
 * Copyright (c) 2007, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lustre/obdclass/linux/linux-obdo.c
 *
 * Object Devices Class Driver
 * These are the only exported functions, they provide some generic
 * infrastructure for managing object devices
 */

#define DEBUG_SUBSYSTEM S_CLASS

#include <linux/module.h>
#include "../../include/obd_class.h"
#include "../../include/lustre/lustre_idl.h"

#include <linux/fs.h>

void obdo_refresh_inode(struct inode *dst, struct obdo *src, u32 valid)
{
	valid &= src->o_valid;

	if (valid & (OBD_MD_FLCTIME | OBD_MD_FLMTIME))
		CDEBUG(D_INODE,
		       "valid %#llx, cur time %lu/%lu, new %llu/%llu\n",
		       src->o_valid, LTIME_S(dst->i_mtime),
		       LTIME_S(dst->i_ctime), src->o_mtime, src->o_ctime);

	if (valid & OBD_MD_FLATIME && src->o_atime > LTIME_S(dst->i_atime))
		LTIME_S(dst->i_atime) = src->o_atime;
	if (valid & OBD_MD_FLMTIME && src->o_mtime > LTIME_S(dst->i_mtime))
		LTIME_S(dst->i_mtime) = src->o_mtime;
	if (valid & OBD_MD_FLCTIME && src->o_ctime > LTIME_S(dst->i_ctime))
		LTIME_S(dst->i_ctime) = src->o_ctime;
	if (valid & OBD_MD_FLSIZE)
		i_size_write(dst, src->o_size);
	/* optimum IO size */
	if (valid & OBD_MD_FLBLKSZ && src->o_blksize > (1 << dst->i_blkbits))
		dst->i_blkbits = ffs(src->o_blksize) - 1;

	if (dst->i_blkbits < PAGE_SHIFT)
		dst->i_blkbits = PAGE_SHIFT;

	/* allocation of space */
	if (valid & OBD_MD_FLBLOCKS && src->o_blocks > dst->i_blocks)
		/*
		 * XXX shouldn't overflow be checked here like in
		 * obdo_to_inode().
		 */
		dst->i_blocks = src->o_blocks;
}
EXPORT_SYMBOL(obdo_refresh_inode);
