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
 * Copyright (c) 2002, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lustre/obdclass/statfs_pack.c
 *
 * (Un)packing of OST/MDS requests
 *
 * Author: Andreas Dilger <adilger@clusterfs.com>
 */

#define DEBUG_SUBSYSTEM S_CLASS

#include <linux/statfs.h>
#include <lustre_export.h>
#include <lustre_net.h>
#include <obd_support.h>
#include <obd_class.h>

void statfs_unpack(struct kstatfs *sfs, struct obd_statfs *osfs)
{
	memset(sfs, 0, sizeof(*sfs));
	sfs->f_type = osfs->os_type;
	sfs->f_blocks = osfs->os_blocks;
	sfs->f_bfree = osfs->os_bfree;
	sfs->f_bavail = osfs->os_bavail;
	sfs->f_files = osfs->os_files;
	sfs->f_ffree = osfs->os_ffree;
	sfs->f_bsize = osfs->os_bsize;
	sfs->f_namelen = osfs->os_namelen;
}
EXPORT_SYMBOL(statfs_unpack);
