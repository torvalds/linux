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
 * Copyright (c) 2003, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lustre/obdclass/obdo.c
 *
 * Object Devices Class Driver
 * These are the only exported functions, they provide some generic
 * infrastructure for managing object devices
 */

#define DEBUG_SUBSYSTEM S_CLASS

#include "../include/obd_class.h"
#include "../include/lustre/lustre_idl.h"

void obdo_set_parent_fid(struct obdo *dst, const struct lu_fid *parent)
{
	dst->o_parent_oid = fid_oid(parent);
	dst->o_parent_seq = fid_seq(parent);
	dst->o_parent_ver = fid_ver(parent);
	dst->o_valid |= OBD_MD_FLGENER | OBD_MD_FLFID;
}
EXPORT_SYMBOL(obdo_set_parent_fid);

/* WARNING: the file systems must take care not to tinker with
 * attributes they don't manage (such as blocks).
 */
void obdo_from_inode(struct obdo *dst, struct inode *src, u32 valid)
{
	u32 newvalid = 0;

	if (valid & (OBD_MD_FLCTIME | OBD_MD_FLMTIME))
		CDEBUG(D_INODE, "valid %x, new time %lu/%lu\n",
		       valid, LTIME_S(src->i_mtime),
		       LTIME_S(src->i_ctime));

	if (valid & OBD_MD_FLATIME) {
		dst->o_atime = LTIME_S(src->i_atime);
		newvalid |= OBD_MD_FLATIME;
	}
	if (valid & OBD_MD_FLMTIME) {
		dst->o_mtime = LTIME_S(src->i_mtime);
		newvalid |= OBD_MD_FLMTIME;
	}
	if (valid & OBD_MD_FLCTIME) {
		dst->o_ctime = LTIME_S(src->i_ctime);
		newvalid |= OBD_MD_FLCTIME;
	}
	if (valid & OBD_MD_FLSIZE) {
		dst->o_size = i_size_read(src);
		newvalid |= OBD_MD_FLSIZE;
	}
	if (valid & OBD_MD_FLBLOCKS) {  /* allocation of space (x512 bytes) */
		dst->o_blocks = src->i_blocks;
		newvalid |= OBD_MD_FLBLOCKS;
	}
	if (valid & OBD_MD_FLBLKSZ) {   /* optimal block size */
		dst->o_blksize = 1 << src->i_blkbits;
		newvalid |= OBD_MD_FLBLKSZ;
	}
	if (valid & OBD_MD_FLTYPE) {
		dst->o_mode = (dst->o_mode & S_IALLUGO) |
			      (src->i_mode & S_IFMT);
		newvalid |= OBD_MD_FLTYPE;
	}
	if (valid & OBD_MD_FLMODE) {
		dst->o_mode = (dst->o_mode & S_IFMT) |
			      (src->i_mode & S_IALLUGO);
		newvalid |= OBD_MD_FLMODE;
	}
	if (valid & OBD_MD_FLUID) {
		dst->o_uid = from_kuid(&init_user_ns, src->i_uid);
		newvalid |= OBD_MD_FLUID;
	}
	if (valid & OBD_MD_FLGID) {
		dst->o_gid = from_kgid(&init_user_ns, src->i_gid);
		newvalid |= OBD_MD_FLGID;
	}
	if (valid & OBD_MD_FLFLAGS) {
		dst->o_flags = src->i_flags;
		newvalid |= OBD_MD_FLFLAGS;
	}
	dst->o_valid |= newvalid;
}
EXPORT_SYMBOL(obdo_from_inode);

void obdo_to_ioobj(const struct obdo *oa, struct obd_ioobj *ioobj)
{
	ioobj->ioo_oid = oa->o_oi;
	if (unlikely(!(oa->o_valid & OBD_MD_FLGROUP)))
		ostid_set_seq_mdt0(&ioobj->ioo_oid);

	/* Since 2.4 this does not contain o_mode in the low 16 bits.
	 * Instead, it holds (bd_md_max_brw - 1) for multi-bulk BRW RPCs
	 */
	ioobj->ioo_max_brw = 0;
}
EXPORT_SYMBOL(obdo_to_ioobj);
