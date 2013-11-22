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
 * lustre/lustre/include/lustre_idmap.h
 *
 * MDS data structures.
 * See also lustre_idl.h for wire formats of requests.
 */

#ifndef _LUSTRE_IDMAP_H
#define _LUSTRE_IDMAP_H

/** \defgroup idmap idmap
 *
 * @{
 */

#include <linux/libcfs/libcfs.h>

#define CFS_NGROUPS_PER_BLOCK   ((int)(PAGE_CACHE_SIZE / sizeof(gid_t)))

#define CFS_GROUP_AT(gi, i) \
	((gi)->blocks[(i) / CFS_NGROUPS_PER_BLOCK][(i) % CFS_NGROUPS_PER_BLOCK])

enum {
	CFS_IC_NOTHING     = 0,    /* convert nothing */
	CFS_IC_ALL	 = 1,    /* convert all items */
	CFS_IC_MAPPED      = 2,    /* convert mapped uid/gid */
	CFS_IC_UNMAPPED    = 3     /* convert unmapped uid/gid */
};

#define  CFS_IDMAP_NOTFOUND     (-1)

#define CFS_IDMAP_HASHSIZE      32

enum lustre_idmap_idx {
	RMT_UIDMAP_IDX,
	LCL_UIDMAP_IDX,
	RMT_GIDMAP_IDX,
	LCL_GIDMAP_IDX,
	CFS_IDMAP_N_HASHES
};

struct lustre_idmap_table {
	spinlock_t	lit_lock;
	struct list_head	lit_idmaps[CFS_IDMAP_N_HASHES][CFS_IDMAP_HASHSIZE];
};

struct lu_ucred;

extern void lustre_groups_from_list(struct group_info *ginfo, gid_t *glist);
extern void lustre_groups_sort(struct group_info *group_info);
extern int lustre_in_group_p(struct lu_ucred *mu, gid_t grp);

extern int lustre_idmap_add(struct lustre_idmap_table *t,
			    uid_t ruid, uid_t luid,
			    gid_t rgid, gid_t lgid);
extern int lustre_idmap_del(struct lustre_idmap_table *t,
			    uid_t ruid, uid_t luid,
			    gid_t rgid, gid_t lgid);
extern int lustre_idmap_lookup_uid(struct lu_ucred *mu,
				   struct lustre_idmap_table *t,
				   int reverse, uid_t uid);
extern int lustre_idmap_lookup_gid(struct lu_ucred *mu,
				   struct lustre_idmap_table *t,
				   int reverse, gid_t gid);
extern struct lustre_idmap_table *lustre_idmap_init(void);
extern void lustre_idmap_fini(struct lustre_idmap_table *t);

/** @} idmap */

#endif
