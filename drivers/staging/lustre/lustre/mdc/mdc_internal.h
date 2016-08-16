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
 * Copyright (c) 2011, 2015 Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 */

#ifndef _MDC_INTERNAL_H
#define _MDC_INTERNAL_H

#include "../include/lustre_mdc.h"
#include "../include/lustre_mds.h"

void lprocfs_mdc_init_vars(struct lprocfs_static_vars *lvars);

void mdc_pack_body(struct ptlrpc_request *req, const struct lu_fid *fid,
		   __u64 valid, int ea_size, __u32 suppgid, int flags);
void mdc_is_subdir_pack(struct ptlrpc_request *req, const struct lu_fid *pfid,
			const struct lu_fid *cfid, int flags);
void mdc_swap_layouts_pack(struct ptlrpc_request *req,
			   struct md_op_data *op_data);
void mdc_readdir_pack(struct ptlrpc_request *req, __u64 pgoff, __u32 size,
		      const struct lu_fid *fid);
void mdc_getattr_pack(struct ptlrpc_request *req, __u64 valid, int flags,
		      struct md_op_data *data, int ea_size);
void mdc_setattr_pack(struct ptlrpc_request *req, struct md_op_data *op_data,
		      void *ea, int ealen, void *ea2, int ea2len);
void mdc_create_pack(struct ptlrpc_request *req, struct md_op_data *op_data,
		     const void *data, int datalen, __u32 mode, __u32 uid,
		     __u32 gid, cfs_cap_t capability, __u64 rdev);
void mdc_open_pack(struct ptlrpc_request *req, struct md_op_data *op_data,
		   __u32 mode, __u64 rdev, __u64 flags, const void *data,
		   int datalen);
void mdc_unlink_pack(struct ptlrpc_request *req, struct md_op_data *op_data);
void mdc_link_pack(struct ptlrpc_request *req, struct md_op_data *op_data);
void mdc_rename_pack(struct ptlrpc_request *req, struct md_op_data *op_data,
		     const char *old, int oldlen, const char *new, int newlen);
void mdc_close_pack(struct ptlrpc_request *req, struct md_op_data *op_data);

/* mdc/mdc_locks.c */
int mdc_set_lock_data(struct obd_export *exp,
		      __u64 *lockh, void *data, __u64 *bits);

int mdc_null_inode(struct obd_export *exp, const struct lu_fid *fid);

int mdc_find_cbdata(struct obd_export *exp, const struct lu_fid *fid,
		    ldlm_iterator_t it, void *data);

int mdc_intent_lock(struct obd_export *exp,
		    struct md_op_data *,
		    void *lmm, int lmmsize,
		    struct lookup_intent *, int,
		    struct ptlrpc_request **reqp,
		    ldlm_blocking_callback cb_blocking,
		    __u64 extra_lock_flags);
int mdc_enqueue(struct obd_export *exp, struct ldlm_enqueue_info *einfo,
		struct lookup_intent *it, struct md_op_data *op_data,
		struct lustre_handle *lockh, void *lmm, int lmmsize,
		struct ptlrpc_request **req, __u64 extra_lock_flags);

int mdc_resource_get_unused(struct obd_export *exp, const struct lu_fid *fid,
			    struct list_head *cancels, enum ldlm_mode  mode,
			    __u64 bits);
/* mdc/mdc_request.c */
int mdc_fid_alloc(struct obd_export *exp, struct lu_fid *fid,
		  struct md_op_data *op_data);
struct obd_client_handle;

int mdc_set_open_replay_data(struct obd_export *exp,
			     struct obd_client_handle *och,
			     struct lookup_intent *it);

void mdc_commit_open(struct ptlrpc_request *req);
void mdc_replay_open(struct ptlrpc_request *req);

int mdc_create(struct obd_export *exp, struct md_op_data *op_data,
	       const void *data, int datalen, int mode, __u32 uid, __u32 gid,
	       cfs_cap_t capability, __u64 rdev,
	       struct ptlrpc_request **request);
int mdc_link(struct obd_export *exp, struct md_op_data *op_data,
	     struct ptlrpc_request **request);
int mdc_rename(struct obd_export *exp, struct md_op_data *op_data,
	       const char *old, int oldlen, const char *new, int newlen,
	       struct ptlrpc_request **request);
int mdc_setattr(struct obd_export *exp, struct md_op_data *op_data,
		void *ea, int ealen, void *ea2, int ea2len,
		struct ptlrpc_request **request, struct md_open_data **mod);
int mdc_unlink(struct obd_export *exp, struct md_op_data *op_data,
	       struct ptlrpc_request **request);
int mdc_cancel_unused(struct obd_export *exp, const struct lu_fid *fid,
		      ldlm_policy_data_t *policy, enum ldlm_mode mode,
		      enum ldlm_cancel_flags flags, void *opaque);

int mdc_revalidate_lock(struct obd_export *exp, struct lookup_intent *it,
			struct lu_fid *fid, __u64 *bits);

int mdc_intent_getattr_async(struct obd_export *exp,
			     struct md_enqueue_info *minfo,
			     struct ldlm_enqueue_info *einfo);

enum ldlm_mode mdc_lock_match(struct obd_export *exp, __u64 flags,
			      const struct lu_fid *fid, enum ldlm_type type,
			      ldlm_policy_data_t *policy, enum ldlm_mode mode,
			      struct lustre_handle *lockh);

static inline int mdc_prep_elc_req(struct obd_export *exp,
				   struct ptlrpc_request *req, int opc,
				   struct list_head *cancels, int count)
{
	return ldlm_prep_elc_req(exp, req, LUSTRE_MDS_VERSION, opc, 0, cancels,
				 count);
}

#endif
