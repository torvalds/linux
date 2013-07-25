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
 * lustre/obdclass/idmap.c
 *
 * Lustre user identity mapping.
 *
 * Author: Fan Yong <fanyong@clusterfs.com>
 */

#define DEBUG_SUBSYSTEM S_SEC

#include <lustre_idmap.h>
#include <md_object.h>
#include <obd_support.h>

#define lustre_get_group_info(group_info) do {	     \
	atomic_inc(&(group_info)->usage);	      \
} while (0)

#define lustre_put_group_info(group_info) do {	     \
	if (atomic_dec_and_test(&(group_info)->usage)) \
		groups_free(group_info);	       \
} while (0)

/*
 * groups_search() is copied from linux kernel!
 * A simple bsearch.
 */
static int lustre_groups_search(group_info_t *group_info,
				gid_t grp)
{
	int left, right;

	if (!group_info)
		return 0;

	left = 0;
	right = group_info->ngroups;
	while (left < right) {
		int mid = (left + right) / 2;
		int cmp = grp - CFS_GROUP_AT(group_info, mid);

		if (cmp > 0)
			left = mid + 1;
		else if (cmp < 0)
			right = mid;
		else
			return 1;
	}
	return 0;
}

void lustre_groups_from_list(group_info_t *ginfo, gid_t *glist)
{
	int i;
	int count = ginfo->ngroups;

	/* fill group_info from gid array */
	for (i = 0; i < ginfo->nblocks && count > 0; i++) {
		int cp_count = min(CFS_NGROUPS_PER_BLOCK, count);
		int off = i * CFS_NGROUPS_PER_BLOCK;
		int len = cp_count * sizeof(*glist);

		memcpy(ginfo->blocks[i], glist + off, len);
		count -= cp_count;
	}
}
EXPORT_SYMBOL(lustre_groups_from_list);

/* groups_sort() is copied from linux kernel! */
/* a simple shell-metzner sort */
void lustre_groups_sort(group_info_t *group_info)
{
	int base, max, stride;
	int gidsetsize = group_info->ngroups;

	for (stride = 1; stride < gidsetsize; stride = 3 * stride + 1)
		; /* nothing */
	stride /= 3;

	while (stride) {
		max = gidsetsize - stride;
		for (base = 0; base < max; base++) {
			int left = base;
			int right = left + stride;
			gid_t tmp = CFS_GROUP_AT(group_info, right);

			while (left >= 0 &&
			       CFS_GROUP_AT(group_info, left) > tmp) {
				CFS_GROUP_AT(group_info, right) =
				    CFS_GROUP_AT(group_info, left);
				right = left;
				left -= stride;
			}
			CFS_GROUP_AT(group_info, right) = tmp;
		}
		stride /= 3;
	}
}
EXPORT_SYMBOL(lustre_groups_sort);

int lustre_in_group_p(struct lu_ucred *mu, gid_t grp)
{
	int rc = 1;

	if (grp != mu->uc_fsgid) {
		group_info_t *group_info = NULL;

		if (mu->uc_ginfo || !mu->uc_identity ||
		    mu->uc_valid == UCRED_OLD)
			if (grp == mu->uc_suppgids[0] ||
			    grp == mu->uc_suppgids[1])
				return 1;

		if (mu->uc_ginfo)
			group_info = mu->uc_ginfo;
		else if (mu->uc_identity)
			group_info = mu->uc_identity->mi_ginfo;

		if (!group_info)
			return 0;

		lustre_get_group_info(group_info);
		rc = lustre_groups_search(group_info, grp);
		lustre_put_group_info(group_info);
	}
	return rc;
}
EXPORT_SYMBOL(lustre_in_group_p);

struct lustre_idmap_entry {
	struct list_head       lie_rmt_uid_hash; /* hashed as lie_rmt_uid; */
	struct list_head       lie_lcl_uid_hash; /* hashed as lie_lcl_uid; */
	struct list_head       lie_rmt_gid_hash; /* hashed as lie_rmt_gid; */
	struct list_head       lie_lcl_gid_hash; /* hashed as lie_lcl_gid; */
	uid_t	    lie_rmt_uid;      /* remote uid */
	uid_t	    lie_lcl_uid;      /* local uid */
	gid_t	    lie_rmt_gid;      /* remote gid */
	gid_t	    lie_lcl_gid;      /* local gid */
};

static inline __u32 lustre_idmap_hashfunc(__u32 id)
{
	return id & (CFS_IDMAP_HASHSIZE - 1);
}

static
struct lustre_idmap_entry *idmap_entry_alloc(uid_t rmt_uid, uid_t lcl_uid,
					     gid_t rmt_gid, gid_t lcl_gid)
{
	struct lustre_idmap_entry *e;

	OBD_ALLOC_PTR(e);
	if (e == NULL)
		return NULL;

	INIT_LIST_HEAD(&e->lie_rmt_uid_hash);
	INIT_LIST_HEAD(&e->lie_lcl_uid_hash);
	INIT_LIST_HEAD(&e->lie_rmt_gid_hash);
	INIT_LIST_HEAD(&e->lie_lcl_gid_hash);
	e->lie_rmt_uid = rmt_uid;
	e->lie_lcl_uid = lcl_uid;
	e->lie_rmt_gid = rmt_gid;
	e->lie_lcl_gid = lcl_gid;

	return e;
}

static void idmap_entry_free(struct lustre_idmap_entry *e)
{
	if (!list_empty(&e->lie_rmt_uid_hash))
		list_del(&e->lie_rmt_uid_hash);
	if (!list_empty(&e->lie_lcl_uid_hash))
		list_del(&e->lie_lcl_uid_hash);
	if (!list_empty(&e->lie_rmt_gid_hash))
		list_del(&e->lie_rmt_gid_hash);
	if (!list_empty(&e->lie_lcl_gid_hash))
		list_del(&e->lie_lcl_gid_hash);
	OBD_FREE_PTR(e);
}

/*
 * return value
 * NULL: not found entry
 * ERR_PTR(-EACCES): found 1(remote):N(local) mapped entry
 * others: found normal entry
 */
static
struct lustre_idmap_entry *idmap_search_entry(struct lustre_idmap_table *t,
					      uid_t rmt_uid, uid_t lcl_uid,
					      gid_t rmt_gid, gid_t lcl_gid)
{
	struct list_head *head;
	struct lustre_idmap_entry *e;

	head = &t->lit_idmaps[RMT_UIDMAP_IDX][lustre_idmap_hashfunc(rmt_uid)];
	list_for_each_entry(e, head, lie_rmt_uid_hash)
		if (e->lie_rmt_uid == rmt_uid) {
			if (e->lie_lcl_uid == lcl_uid) {
				if (e->lie_rmt_gid == rmt_gid &&
				    e->lie_lcl_gid == lcl_gid)
					/* must be quaternion match */
					return e;
			} else {
				/* 1:N uid mapping */
				CERROR("rmt uid %u already be mapped to %u"
				       " (new %u)\n", e->lie_rmt_uid,
				       e->lie_lcl_uid, lcl_uid);
				return ERR_PTR(-EACCES);
			}
		}

	head = &t->lit_idmaps[RMT_GIDMAP_IDX][lustre_idmap_hashfunc(rmt_gid)];
	list_for_each_entry(e, head, lie_rmt_gid_hash)
		if (e->lie_rmt_gid == rmt_gid) {
			if (e->lie_lcl_gid == lcl_gid) {
				if (unlikely(e->lie_rmt_uid == rmt_uid &&
				    e->lie_lcl_uid == lcl_uid))
					/* after uid mapping search above,
					 * we should never come here */
					LBUG();
			} else {
				/* 1:N gid mapping */
				CERROR("rmt gid %u already be mapped to %u"
				       " (new %u)\n", e->lie_rmt_gid,
				       e->lie_lcl_gid, lcl_gid);
				return ERR_PTR(-EACCES);
			}
		}

	return NULL;
}

static __u32 idmap_lookup_uid(struct list_head *hash, int reverse,
			      __u32 uid)
{
	struct list_head *head = &hash[lustre_idmap_hashfunc(uid)];
	struct lustre_idmap_entry *e;

	if (!reverse) {
		list_for_each_entry(e, head, lie_rmt_uid_hash)
			if (e->lie_rmt_uid == uid)
				return e->lie_lcl_uid;
	} else {
		list_for_each_entry(e, head, lie_lcl_uid_hash)
			if (e->lie_lcl_uid == uid)
				return e->lie_rmt_uid;
	}

	return CFS_IDMAP_NOTFOUND;
}

static __u32 idmap_lookup_gid(struct list_head *hash, int reverse, __u32 gid)
{
	struct list_head *head = &hash[lustre_idmap_hashfunc(gid)];
	struct lustre_idmap_entry *e;

	if (!reverse) {
		list_for_each_entry(e, head, lie_rmt_gid_hash)
			if (e->lie_rmt_gid == gid)
				return e->lie_lcl_gid;
	} else {
		list_for_each_entry(e, head, lie_lcl_gid_hash)
			if (e->lie_lcl_gid == gid)
				return e->lie_rmt_gid;
	}

	return CFS_IDMAP_NOTFOUND;
}

int lustre_idmap_add(struct lustre_idmap_table *t,
		     uid_t ruid, uid_t luid,
		     gid_t rgid, gid_t lgid)
{
	struct lustre_idmap_entry *e0, *e1;

	LASSERT(t);

	spin_lock(&t->lit_lock);
	e0 = idmap_search_entry(t, ruid, luid, rgid, lgid);
	spin_unlock(&t->lit_lock);
	if (!e0) {
		e0 = idmap_entry_alloc(ruid, luid, rgid, lgid);
		if (!e0)
			return -ENOMEM;

		spin_lock(&t->lit_lock);
		e1 = idmap_search_entry(t, ruid, luid, rgid, lgid);
		if (e1 == NULL) {
			list_add_tail(&e0->lie_rmt_uid_hash,
					  &t->lit_idmaps[RMT_UIDMAP_IDX]
					  [lustre_idmap_hashfunc(ruid)]);
			list_add_tail(&e0->lie_lcl_uid_hash,
					  &t->lit_idmaps[LCL_UIDMAP_IDX]
					  [lustre_idmap_hashfunc(luid)]);
			list_add_tail(&e0->lie_rmt_gid_hash,
					  &t->lit_idmaps[RMT_GIDMAP_IDX]
					  [lustre_idmap_hashfunc(rgid)]);
			list_add_tail(&e0->lie_lcl_gid_hash,
					  &t->lit_idmaps[LCL_GIDMAP_IDX]
					  [lustre_idmap_hashfunc(lgid)]);
		}
		spin_unlock(&t->lit_lock);
		if (e1 != NULL) {
			idmap_entry_free(e0);
			if (IS_ERR(e1))
				return PTR_ERR(e1);
		}
	} else if (IS_ERR(e0)) {
		return PTR_ERR(e0);
	}

	return 0;
}
EXPORT_SYMBOL(lustre_idmap_add);

int lustre_idmap_del(struct lustre_idmap_table *t,
		    uid_t ruid, uid_t luid,
		    gid_t rgid, gid_t lgid)
{
	struct lustre_idmap_entry *e;
	int rc = 0;

	LASSERT(t);

	spin_lock(&t->lit_lock);
	e = idmap_search_entry(t, ruid, luid, rgid, lgid);
	if (IS_ERR(e))
		rc = PTR_ERR(e);
	else if (e)
		idmap_entry_free(e);
	spin_unlock(&t->lit_lock);

	return rc;
}
EXPORT_SYMBOL(lustre_idmap_del);

int lustre_idmap_lookup_uid(struct lu_ucred *mu,
			    struct lustre_idmap_table *t,
			    int reverse, uid_t uid)
{
	struct list_head *hash;

	if (mu && (mu->uc_valid == UCRED_OLD || mu->uc_valid == UCRED_NEW)) {
		if (!reverse) {
			if (uid == mu->uc_o_uid)
				return mu->uc_uid;
			else if (uid == mu->uc_o_fsuid)
				return mu->uc_fsuid;
		} else {
			if (uid == mu->uc_uid)
				return mu->uc_o_uid;
			else if (uid == mu->uc_fsuid)
				return mu->uc_o_fsuid;
		}
	}

	if (t == NULL)
		return CFS_IDMAP_NOTFOUND;

	hash = t->lit_idmaps[reverse ? LCL_UIDMAP_IDX : RMT_UIDMAP_IDX];

	spin_lock(&t->lit_lock);
	uid = idmap_lookup_uid(hash, reverse, uid);
	spin_unlock(&t->lit_lock);

	return uid;
}
EXPORT_SYMBOL(lustre_idmap_lookup_uid);

int lustre_idmap_lookup_gid(struct lu_ucred *mu, struct lustre_idmap_table *t,
			    int reverse, gid_t gid)
{
	struct list_head *hash;

	if (mu && (mu->uc_valid == UCRED_OLD || mu->uc_valid == UCRED_NEW)) {
		if (!reverse) {
			if (gid == mu->uc_o_gid)
				return mu->uc_gid;
			else if (gid == mu->uc_o_fsgid)
				return mu->uc_fsgid;
		} else {
			if (gid == mu->uc_gid)
				return mu->uc_o_gid;
			else if (gid == mu->uc_fsgid)
				return mu->uc_o_fsgid;
		}
	}

	if (t == NULL)
		return CFS_IDMAP_NOTFOUND;

	hash = t->lit_idmaps[reverse ? LCL_GIDMAP_IDX : RMT_GIDMAP_IDX];

	spin_lock(&t->lit_lock);
	gid = idmap_lookup_gid(hash, reverse, gid);
	spin_unlock(&t->lit_lock);

	return gid;
}
EXPORT_SYMBOL(lustre_idmap_lookup_gid);

struct lustre_idmap_table *lustre_idmap_init(void)
{
	struct lustre_idmap_table *t;
	int i, j;

	OBD_ALLOC_PTR(t);
	if(unlikely(t == NULL))
		return (ERR_PTR(-ENOMEM));

	spin_lock_init(&t->lit_lock);
	for (i = 0; i < ARRAY_SIZE(t->lit_idmaps); i++)
		for (j = 0; j < ARRAY_SIZE(t->lit_idmaps[i]); j++)
			INIT_LIST_HEAD(&t->lit_idmaps[i][j]);

	return t;
}
EXPORT_SYMBOL(lustre_idmap_init);

void lustre_idmap_fini(struct lustre_idmap_table *t)
{
	struct list_head *list;
	struct lustre_idmap_entry *e;
	int i;
	LASSERT(t);

	list = t->lit_idmaps[RMT_UIDMAP_IDX];
	spin_lock(&t->lit_lock);
	for (i = 0; i < CFS_IDMAP_HASHSIZE; i++)
		while (!list_empty(&list[i])) {
			e = list_entry(list[i].next,
					   struct lustre_idmap_entry,
					   lie_rmt_uid_hash);
			idmap_entry_free(e);
		}
	spin_unlock(&t->lit_lock);

	OBD_FREE_PTR(t);
}
EXPORT_SYMBOL(lustre_idmap_fini);
