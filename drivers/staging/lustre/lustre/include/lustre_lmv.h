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
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License version 2 for more details.  A copy is
 * included in the COPYING file that accompanied this code.
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; If not, see
 * http://www.sun.com/software/products/lustre/docs/GPLv2.pdf
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2013, Intel Corporation.
 */
/*
 * lustre/include/lustre_lmv.h
 *
 * Lustre LMV structures and functions.
 *
 * Author: Di Wang <di.wang@intel.com>
 */

#ifndef _LUSTRE_LMV_H
#define _LUSTRE_LMV_H
#include <uapi/linux/lustre/lustre_idl.h>

struct lmv_oinfo {
	struct lu_fid	lmo_fid;
	u32		lmo_mds;
	struct inode	*lmo_root;
};

struct lmv_stripe_md {
	__u32	lsm_md_magic;
	__u32	lsm_md_stripe_count;
	__u32	lsm_md_master_mdt_index;
	__u32	lsm_md_hash_type;
	__u32	lsm_md_layout_version;
	__u32	lsm_md_default_count;
	__u32	lsm_md_default_index;
	char	lsm_md_pool_name[LOV_MAXPOOLNAME + 1];
	struct lmv_oinfo lsm_md_oinfo[0];
};

static inline bool
lsm_md_eq(const struct lmv_stripe_md *lsm1, const struct lmv_stripe_md *lsm2)
{
	__u32 idx;

	if (lsm1->lsm_md_magic != lsm2->lsm_md_magic ||
	    lsm1->lsm_md_stripe_count != lsm2->lsm_md_stripe_count ||
	    lsm1->lsm_md_master_mdt_index != lsm2->lsm_md_master_mdt_index ||
	    lsm1->lsm_md_hash_type != lsm2->lsm_md_hash_type ||
	    lsm1->lsm_md_layout_version != lsm2->lsm_md_layout_version ||
	    !strcmp(lsm1->lsm_md_pool_name, lsm2->lsm_md_pool_name))
		return false;

	for (idx = 0; idx < lsm1->lsm_md_stripe_count; idx++) {
		if (!lu_fid_eq(&lsm1->lsm_md_oinfo[idx].lmo_fid,
			       &lsm2->lsm_md_oinfo[idx].lmo_fid))
			return false;
	}

	return true;
}

union lmv_mds_md;

void lmv_free_memmd(struct lmv_stripe_md *lsm);

static inline void lmv1_le_to_cpu(struct lmv_mds_md_v1 *lmv_dst,
				  const struct lmv_mds_md_v1 *lmv_src)
{
	__u32 i;

	lmv_dst->lmv_magic = le32_to_cpu(lmv_src->lmv_magic);
	lmv_dst->lmv_stripe_count = le32_to_cpu(lmv_src->lmv_stripe_count);
	lmv_dst->lmv_master_mdt_index =
		le32_to_cpu(lmv_src->lmv_master_mdt_index);
	lmv_dst->lmv_hash_type = le32_to_cpu(lmv_src->lmv_hash_type);
	lmv_dst->lmv_layout_version = le32_to_cpu(lmv_src->lmv_layout_version);

	for (i = 0; i < lmv_src->lmv_stripe_count; i++)
		fid_le_to_cpu(&lmv_dst->lmv_stripe_fids[i],
			      &lmv_src->lmv_stripe_fids[i]);
}

static inline void lmv_le_to_cpu(union lmv_mds_md *lmv_dst,
				 const union lmv_mds_md *lmv_src)
{
	switch (le32_to_cpu(lmv_src->lmv_magic)) {
	case LMV_MAGIC_V1:
		lmv1_le_to_cpu(&lmv_dst->lmv_md_v1, &lmv_src->lmv_md_v1);
		break;
	default:
		break;
	}
}

/* This hash is only for testing purpose */
static inline unsigned int
lmv_hash_all_chars(unsigned int count, const char *name, int namelen)
{
	const unsigned char *p = (const unsigned char *)name;
	unsigned int c = 0;

	while (--namelen >= 0)
		c += p[namelen];

	c = c % count;

	return c;
}

static inline unsigned int
lmv_hash_fnv1a(unsigned int count, const char *name, int namelen)
{
	__u64 hash;

	hash = lustre_hash_fnv_1a_64(name, namelen);

	return do_div(hash, count);
}

static inline int lmv_name_to_stripe_index(__u32 lmv_hash_type,
					   unsigned int stripe_count,
					   const char *name, int namelen)
{
	__u32 hash_type = lmv_hash_type & LMV_HASH_TYPE_MASK;
	int idx;

	LASSERT(namelen > 0);
	if (stripe_count <= 1)
		return 0;

	/* for migrating object, always start from 0 stripe */
	if (lmv_hash_type & LMV_HASH_FLAG_MIGRATION)
		return 0;

	switch (hash_type) {
	case LMV_HASH_TYPE_ALL_CHARS:
		idx = lmv_hash_all_chars(stripe_count, name, namelen);
		break;
	case LMV_HASH_TYPE_FNV_1A_64:
		idx = lmv_hash_fnv1a(stripe_count, name, namelen);
		break;
	default:
		idx = -EBADFD;
		break;
	}
	CDEBUG(D_INFO, "name %.*s hash_type %d idx %d\n", namelen, name,
	       hash_type, idx);

	return idx;
}

static inline bool lmv_is_known_hash_type(__u32 type)
{
	return (type & LMV_HASH_TYPE_MASK) == LMV_HASH_TYPE_FNV_1A_64 ||
	       (type & LMV_HASH_TYPE_MASK) == LMV_HASH_TYPE_ALL_CHARS;
}

#endif
