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
#include "lustre/lustre_idl.h"

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
	char	lsm_md_pool_name[LOV_MAXPOOLNAME];
	struct lmv_oinfo lsm_md_oinfo[0];
};

union lmv_mds_md;

int lmv_unpack_md(struct obd_export *exp, struct lmv_stripe_md **lsmp,
		  const union lmv_mds_md *lmm, int stripe_count);

static inline int lmv_alloc_memmd(struct lmv_stripe_md **lsmp, int stripe_count)
{
	return lmv_unpack_md(NULL, lsmp, NULL, stripe_count);
}

static inline void lmv_free_memmd(struct lmv_stripe_md *lsm)
{
	lmv_unpack_md(NULL, &lsm, NULL, 0);
}

static inline void lmv1_cpu_to_le(struct lmv_mds_md_v1 *lmv_dst,
				  const struct lmv_mds_md_v1 *lmv_src)
{
	int i;

	lmv_dst->lmv_magic = cpu_to_le32(lmv_src->lmv_magic);
	lmv_dst->lmv_stripe_count = cpu_to_le32(lmv_src->lmv_stripe_count);
	lmv_dst->lmv_master_mdt_index =
		cpu_to_le32(lmv_src->lmv_master_mdt_index);
	lmv_dst->lmv_hash_type = cpu_to_le32(lmv_src->lmv_hash_type);
	lmv_dst->lmv_layout_version = cpu_to_le32(lmv_src->lmv_layout_version);

	for (i = 0; i < lmv_src->lmv_stripe_count; i++)
		fid_cpu_to_le(&lmv_dst->lmv_stripe_fids[i],
			      &lmv_src->lmv_stripe_fids[i]);
}

static inline void lmv1_le_to_cpu(struct lmv_mds_md_v1 *lmv_dst,
				  const struct lmv_mds_md_v1 *lmv_src)
{
	int i;

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

static inline void lmv_cpu_to_le(union lmv_mds_md *lmv_dst,
				 const union lmv_mds_md *lmv_src)
{
	switch (lmv_src->lmv_magic) {
	case LMV_MAGIC_V1:
		lmv1_cpu_to_le(&lmv_dst->lmv_md_v1, &lmv_src->lmv_md_v1);
		break;
	default:
		break;
	}
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

#endif
