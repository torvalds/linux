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
 * Copyright (c) 2007, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, 2015, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lustre/lov/lov_ea.c
 *
 * Author: Wang Di <wangdi@clusterfs.com>
 */

#define DEBUG_SUBSYSTEM S_LOV

#include <asm/div64.h>
#include "../../include/linux/libcfs/libcfs.h"

#include "../include/obd_class.h"
#include "../include/lustre/lustre_idl.h"

#include "lov_internal.h"

static int lsm_lmm_verify_common(struct lov_mds_md *lmm, int lmm_bytes,
				 __u16 stripe_count)
{
	if (stripe_count > LOV_V1_INSANE_STRIPE_COUNT) {
		CERROR("bad stripe count %d\n", stripe_count);
		lov_dump_lmm_common(D_WARNING, lmm);
		return -EINVAL;
	}

	if (lmm_oi_id(&lmm->lmm_oi) == 0) {
		CERROR("zero object id\n");
		lov_dump_lmm_common(D_WARNING, lmm);
		return -EINVAL;
	}

	if (lov_pattern(le32_to_cpu(lmm->lmm_pattern)) != LOV_PATTERN_RAID0) {
		CERROR("bad striping pattern\n");
		lov_dump_lmm_common(D_WARNING, lmm);
		return -EINVAL;
	}

	if (lmm->lmm_stripe_size == 0 ||
	    (le32_to_cpu(lmm->lmm_stripe_size) &
	     (LOV_MIN_STRIPE_SIZE - 1)) != 0) {
		CERROR("bad stripe size %u\n",
		       le32_to_cpu(lmm->lmm_stripe_size));
		lov_dump_lmm_common(D_WARNING, lmm);
		return -EINVAL;
	}
	return 0;
}

struct lov_stripe_md *lsm_alloc_plain(u16 stripe_count)
{
	size_t oinfo_ptrs_size, lsm_size;
	struct lov_stripe_md *lsm;
	struct lov_oinfo     *loi;
	int i;

	LASSERT(stripe_count <= LOV_MAX_STRIPE_COUNT);

	oinfo_ptrs_size = sizeof(struct lov_oinfo *) * stripe_count;
	lsm_size = sizeof(*lsm) + oinfo_ptrs_size;

	lsm = libcfs_kvzalloc(lsm_size, GFP_NOFS);
	if (!lsm)
		return NULL;

	for (i = 0; i < stripe_count; i++) {
		loi = kmem_cache_zalloc(lov_oinfo_slab, GFP_NOFS);
		if (!loi)
			goto err;
		lsm->lsm_oinfo[i] = loi;
	}
	lsm->lsm_stripe_count = stripe_count;
	return lsm;

err:
	while (--i >= 0)
		kmem_cache_free(lov_oinfo_slab, lsm->lsm_oinfo[i]);
	kvfree(lsm);
	return NULL;
}

void lsm_free_plain(struct lov_stripe_md *lsm)
{
	__u16 stripe_count = lsm->lsm_stripe_count;
	int i;

	for (i = 0; i < stripe_count; i++)
		kmem_cache_free(lov_oinfo_slab, lsm->lsm_oinfo[i]);
	kvfree(lsm);
}

/*
 * Find minimum stripe maxbytes value.  For inactive or
 * reconnecting targets use LUSTRE_EXT3_STRIPE_MAXBYTES.
 */
static loff_t lov_tgt_maxbytes(struct lov_tgt_desc *tgt)
{
	loff_t maxbytes = LUSTRE_EXT3_STRIPE_MAXBYTES;
	struct obd_import *imp;

	if (!tgt->ltd_active)
		return maxbytes;

	imp = tgt->ltd_obd->u.cli.cl_import;
	if (!imp)
		return maxbytes;

	spin_lock(&imp->imp_lock);
	if (imp->imp_state == LUSTRE_IMP_FULL &&
	    (imp->imp_connect_data.ocd_connect_flags & OBD_CONNECT_MAXBYTES) &&
	     imp->imp_connect_data.ocd_maxbytes > 0)
		maxbytes = imp->imp_connect_data.ocd_maxbytes;

	spin_unlock(&imp->imp_lock);

	return maxbytes;
}

static int lsm_unpackmd_common(struct lov_obd *lov,
			       struct lov_stripe_md *lsm,
			       struct lov_mds_md *lmm,
			       struct lov_ost_data_v1 *objects)
{
	loff_t min_stripe_maxbytes = 0;
	unsigned int stripe_count;
	struct lov_oinfo *loi;
	loff_t lov_bytes;
	unsigned int i;

	/*
	 * This supposes lov_mds_md_v1/v3 first fields are
	 * are the same
	 */
	lmm_oi_le_to_cpu(&lsm->lsm_oi, &lmm->lmm_oi);
	lsm->lsm_stripe_size = le32_to_cpu(lmm->lmm_stripe_size);
	lsm->lsm_pattern = le32_to_cpu(lmm->lmm_pattern);
	lsm->lsm_layout_gen = le16_to_cpu(lmm->lmm_layout_gen);
	lsm->lsm_pool_name[0] = '\0';

	stripe_count = lsm_is_released(lsm) ? 0 : lsm->lsm_stripe_count;

	for (i = 0; i < stripe_count; i++) {
		loi = lsm->lsm_oinfo[i];
		ostid_le_to_cpu(&objects[i].l_ost_oi, &loi->loi_oi);
		loi->loi_ost_idx = le32_to_cpu(objects[i].l_ost_idx);
		loi->loi_ost_gen = le32_to_cpu(objects[i].l_ost_gen);
		if (lov_oinfo_is_dummy(loi))
			continue;

		if (loi->loi_ost_idx >= lov->desc.ld_tgt_count &&
		    !lov2obd(lov)->obd_process_conf) {
			CERROR("%s: OST index %d more than OST count %d\n",
			       (char *)lov->desc.ld_uuid.uuid,
			       loi->loi_ost_idx, lov->desc.ld_tgt_count);
			lov_dump_lmm_v1(D_WARNING, lmm);
			return -EINVAL;
		}

		if (!lov->lov_tgts[loi->loi_ost_idx]) {
			CERROR("%s: OST index %d missing\n",
			       (char *)lov->desc.ld_uuid.uuid,
			       loi->loi_ost_idx);
			lov_dump_lmm_v1(D_WARNING, lmm);
			continue;
		}

		lov_bytes = lov_tgt_maxbytes(lov->lov_tgts[loi->loi_ost_idx]);
		if (min_stripe_maxbytes == 0 || lov_bytes < min_stripe_maxbytes)
			min_stripe_maxbytes = lov_bytes;
	}

	if (min_stripe_maxbytes == 0)
		min_stripe_maxbytes = LUSTRE_EXT3_STRIPE_MAXBYTES;

	stripe_count = lsm->lsm_stripe_count ?: lov->desc.ld_tgt_count;
	lov_bytes = min_stripe_maxbytes * stripe_count;

	if (lov_bytes < min_stripe_maxbytes) /* handle overflow */
		lsm->lsm_maxbytes = MAX_LFS_FILESIZE;
	else
		lsm->lsm_maxbytes = lov_bytes;

	return 0;
}

static void
lsm_stripe_by_index_plain(struct lov_stripe_md *lsm, int *stripeno,
			  loff_t *lov_off, loff_t *swidth)
{
	if (swidth)
		*swidth = (u64)lsm->lsm_stripe_size * lsm->lsm_stripe_count;
}

static void
lsm_stripe_by_offset_plain(struct lov_stripe_md *lsm, int *stripeno,
			   loff_t *lov_off, loff_t *swidth)
{
	if (swidth)
		*swidth = (u64)lsm->lsm_stripe_size * lsm->lsm_stripe_count;
}

static int lsm_lmm_verify_v1(struct lov_mds_md_v1 *lmm, int lmm_bytes,
			     __u16 *stripe_count)
{
	if (lmm_bytes < sizeof(*lmm)) {
		CERROR("lov_mds_md_v1 too small: %d, need at least %d\n",
		       lmm_bytes, (int)sizeof(*lmm));
		return -EINVAL;
	}

	*stripe_count = le16_to_cpu(lmm->lmm_stripe_count);
	if (le32_to_cpu(lmm->lmm_pattern) & LOV_PATTERN_F_RELEASED)
		*stripe_count = 0;

	if (lmm_bytes < lov_mds_md_size(*stripe_count, LOV_MAGIC_V1)) {
		CERROR("LOV EA V1 too small: %d, need %d\n",
		       lmm_bytes, lov_mds_md_size(*stripe_count, LOV_MAGIC_V1));
		lov_dump_lmm_common(D_WARNING, lmm);
		return -EINVAL;
	}

	return lsm_lmm_verify_common(lmm, lmm_bytes, *stripe_count);
}

static int lsm_unpackmd_v1(struct lov_obd *lov, struct lov_stripe_md *lsm,
			   struct lov_mds_md_v1 *lmm)
{
	return lsm_unpackmd_common(lov, lsm, lmm, lmm->lmm_objects);
}

const struct lsm_operations lsm_v1_ops = {
	.lsm_free	    = lsm_free_plain,
	.lsm_stripe_by_index    = lsm_stripe_by_index_plain,
	.lsm_stripe_by_offset   = lsm_stripe_by_offset_plain,
	.lsm_lmm_verify	 = lsm_lmm_verify_v1,
	.lsm_unpackmd	   = lsm_unpackmd_v1,
};

static int lsm_lmm_verify_v3(struct lov_mds_md *lmmv1, int lmm_bytes,
			     __u16 *stripe_count)
{
	struct lov_mds_md_v3 *lmm;

	lmm = (struct lov_mds_md_v3 *)lmmv1;

	if (lmm_bytes < sizeof(*lmm)) {
		CERROR("lov_mds_md_v3 too small: %d, need at least %d\n",
		       lmm_bytes, (int)sizeof(*lmm));
		return -EINVAL;
	}

	*stripe_count = le16_to_cpu(lmm->lmm_stripe_count);
	if (le32_to_cpu(lmm->lmm_pattern) & LOV_PATTERN_F_RELEASED)
		*stripe_count = 0;

	if (lmm_bytes < lov_mds_md_size(*stripe_count, LOV_MAGIC_V3)) {
		CERROR("LOV EA V3 too small: %d, need %d\n",
		       lmm_bytes, lov_mds_md_size(*stripe_count, LOV_MAGIC_V3));
		lov_dump_lmm_common(D_WARNING, lmm);
		return -EINVAL;
	}

	return lsm_lmm_verify_common((struct lov_mds_md_v1 *)lmm, lmm_bytes,
				     *stripe_count);
}

static int lsm_unpackmd_v3(struct lov_obd *lov, struct lov_stripe_md *lsm,
			   struct lov_mds_md *lmm)
{
	struct lov_mds_md_v3 *lmm_v3 = (struct lov_mds_md_v3 *)lmm;
	size_t cplen = 0;
	int rc;

	rc = lsm_unpackmd_common(lov, lsm, lmm, lmm_v3->lmm_objects);
	if (rc)
		return rc;

	cplen = strlcpy(lsm->lsm_pool_name, lmm_v3->lmm_pool_name,
			sizeof(lsm->lsm_pool_name));
	if (cplen >= sizeof(lsm->lsm_pool_name))
		return -E2BIG;

	return 0;
}

const struct lsm_operations lsm_v3_ops = {
	.lsm_free	    = lsm_free_plain,
	.lsm_stripe_by_index    = lsm_stripe_by_index_plain,
	.lsm_stripe_by_offset   = lsm_stripe_by_offset_plain,
	.lsm_lmm_verify	 = lsm_lmm_verify_v3,
	.lsm_unpackmd	   = lsm_unpackmd_v3,
};

void dump_lsm(unsigned int level, const struct lov_stripe_md *lsm)
{
	CDEBUG(level, "lsm %p, objid " DOSTID ", maxbytes %#llx, magic 0x%08X, stripe_size %u, stripe_count %u, refc: %d, layout_gen %u, pool [" LOV_POOLNAMEF "]\n",
	       lsm,
	       POSTID(&lsm->lsm_oi), lsm->lsm_maxbytes, lsm->lsm_magic,
	       lsm->lsm_stripe_size, lsm->lsm_stripe_count,
	       atomic_read(&lsm->lsm_refc), lsm->lsm_layout_gen,
	       lsm->lsm_pool_name);
}
