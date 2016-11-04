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
 * Copyright (c) 2011, 2014, Intel Corporation.
 */

#define DEBUG_SUBSYSTEM S_LLITE

#include "../include/lustre/lustre_idl.h"
#include "../include/cl_object.h"
#include "../include/obd.h"
#include "../include/obd_support.h"
#include "llite_internal.h"
#include "vvp_internal.h"

static inline struct vvp_req *cl2vvp_req(const struct cl_req_slice *slice)
{
	return container_of0(slice, struct vvp_req, vrq_cl);
}

/**
 * Implementation of struct cl_req_operations::cro_attr_set() for VVP
 * layer. VVP is responsible for
 *
 *    - o_[mac]time
 *
 *    - o_mode
 *
 *    - o_parent_seq
 *
 *    - o_[ug]id
 *
 *    - o_parent_oid
 *
 *    - o_parent_ver
 *
 *    - o_ioepoch,
 *
 */
static void vvp_req_attr_set(const struct lu_env *env,
			     const struct cl_req_slice *slice,
			     const struct cl_object *obj,
			     struct cl_req_attr *attr, u64 flags)
{
	struct inode *inode;
	struct obdo  *oa;
	u32	      valid_flags;

	oa = attr->cra_oa;
	inode = vvp_object_inode(obj);
	valid_flags = OBD_MD_FLTYPE;

	if (slice->crs_req->crq_type == CRT_WRITE) {
		if (flags & OBD_MD_FLEPOCH) {
			oa->o_valid |= OBD_MD_FLEPOCH;
			oa->o_ioepoch = ll_i2info(inode)->lli_ioepoch;
			valid_flags |= OBD_MD_FLMTIME | OBD_MD_FLCTIME |
				       OBD_MD_FLUID | OBD_MD_FLGID;
		}
	}
	obdo_from_inode(oa, inode, valid_flags & flags);
	obdo_set_parent_fid(oa, &ll_i2info(inode)->lli_fid);
	if (OBD_FAIL_CHECK(OBD_FAIL_LFSCK_INVALID_PFID))
		oa->o_parent_oid++;
	memcpy(attr->cra_jobid, ll_i2info(inode)->lli_jobid,
	       LUSTRE_JOBID_SIZE);
}

static void vvp_req_completion(const struct lu_env *env,
			       const struct cl_req_slice *slice, int ioret)
{
	struct vvp_req *vrq;

	if (ioret > 0)
		cl_stats_tally(slice->crs_dev, slice->crs_req->crq_type, ioret);

	vrq = cl2vvp_req(slice);
	kmem_cache_free(vvp_req_kmem, vrq);
}

static const struct cl_req_operations vvp_req_ops = {
	.cro_attr_set   = vvp_req_attr_set,
	.cro_completion = vvp_req_completion
};

int vvp_req_init(const struct lu_env *env, struct cl_device *dev,
		 struct cl_req *req)
{
	struct vvp_req *vrq;
	int result;

	vrq = kmem_cache_zalloc(vvp_req_kmem, GFP_NOFS);
	if (vrq) {
		cl_req_slice_add(req, &vrq->vrq_cl, dev, &vvp_req_ops);
		result = 0;
	} else {
		result = -ENOMEM;
	}
	return result;
}
