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
 * Copyright (c) 2011, 2015, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 */

#define DEBUG_SUBSYSTEM S_MDC
#include "../include/lustre_net.h"
#include "../include/lustre/lustre_idl.h"
#include "mdc_internal.h"

static void __mdc_pack_body(struct mdt_body *b, __u32 suppgid)
{
	b->mbo_suppgid = suppgid;
	b->mbo_uid = from_kuid(&init_user_ns, current_uid());
	b->mbo_gid = from_kgid(&init_user_ns, current_gid());
	b->mbo_fsuid = from_kuid(&init_user_ns, current_fsuid());
	b->mbo_fsgid = from_kgid(&init_user_ns, current_fsgid());
	b->mbo_capability = cfs_curproc_cap_pack();
}

void mdc_is_subdir_pack(struct ptlrpc_request *req, const struct lu_fid *pfid,
			const struct lu_fid *cfid, int flags)
{
	struct mdt_body *b = req_capsule_client_get(&req->rq_pill,
						    &RMF_MDT_BODY);

	if (pfid) {
		b->mbo_fid1 = *pfid;
		b->mbo_valid = OBD_MD_FLID;
	}
	if (cfid)
		b->mbo_fid2 = *cfid;
	b->mbo_flags = flags;
}

void mdc_swap_layouts_pack(struct ptlrpc_request *req,
			   struct md_op_data *op_data)
{
	struct mdt_body *b = req_capsule_client_get(&req->rq_pill,
						    &RMF_MDT_BODY);

	__mdc_pack_body(b, op_data->op_suppgids[0]);
	b->mbo_fid1 = op_data->op_fid1;
	b->mbo_fid2 = op_data->op_fid2;
	b->mbo_valid |= OBD_MD_FLID;
}

void mdc_pack_body(struct ptlrpc_request *req, const struct lu_fid *fid,
		   __u64 valid, int ea_size, __u32 suppgid, int flags)
{
	struct mdt_body *b = req_capsule_client_get(&req->rq_pill,
						    &RMF_MDT_BODY);
	b->mbo_valid = valid;
	b->mbo_eadatasize = ea_size;
	b->mbo_flags = flags;
	__mdc_pack_body(b, suppgid);
	if (fid) {
		b->mbo_fid1 = *fid;
		b->mbo_valid |= OBD_MD_FLID;
	}
}

/**
 * Pack a name (path component) into a request
 *
 * \param[in] req	request
 * \param[in] field	request field (usually RMF_NAME)
 * \param[in] name	path component
 * \param[in] name_len	length of path component
 *
 * \a field must be present in \a req and of size \a name_len + 1.
 *
 * \a name must be '\0' terminated of length \a name_len and represent
 * a single path component (not contain '/').
 */
static void mdc_pack_name(struct ptlrpc_request *req,
			  const struct req_msg_field *field,
			  const char *name, size_t name_len)
{
	size_t buf_size;
	size_t cpy_len;
	char *buf;

	buf = req_capsule_client_get(&req->rq_pill, field);
	buf_size = req_capsule_get_size(&req->rq_pill, field, RCL_CLIENT);

	LASSERT(name && name_len && buf && buf_size == name_len + 1);

	cpy_len = strlcpy(buf, name, buf_size);

	LASSERT(cpy_len == name_len && lu_name_is_valid_2(buf, cpy_len));
}

void mdc_readdir_pack(struct ptlrpc_request *req, __u64 pgoff,
		      __u32 size, const struct lu_fid *fid)
{
	struct mdt_body *b = req_capsule_client_get(&req->rq_pill,
						    &RMF_MDT_BODY);
	b->mbo_fid1 = *fid;
	b->mbo_valid |= OBD_MD_FLID;
	b->mbo_size = pgoff;		       /* !! */
	b->mbo_nlink = size;			/* !! */
	__mdc_pack_body(b, -1);
	b->mbo_mode = LUDA_FID | LUDA_TYPE;
}

/* packing of MDS records */
void mdc_create_pack(struct ptlrpc_request *req, struct md_op_data *op_data,
		     const void *data, int datalen, __u32 mode,
		     __u32 uid, __u32 gid, cfs_cap_t cap_effective, __u64 rdev)
{
	struct mdt_rec_create	*rec;
	char			*tmp;
	__u64			 flags;

	CLASSERT(sizeof(struct mdt_rec_reint) == sizeof(struct mdt_rec_create));
	rec = req_capsule_client_get(&req->rq_pill, &RMF_REC_REINT);

	rec->cr_opcode   = REINT_CREATE;
	rec->cr_fsuid    = uid;
	rec->cr_fsgid    = gid;
	rec->cr_cap      = cap_effective;
	rec->cr_fid1     = op_data->op_fid1;
	rec->cr_fid2     = op_data->op_fid2;
	rec->cr_mode     = mode;
	rec->cr_rdev     = rdev;
	rec->cr_time     = op_data->op_mod_time;
	rec->cr_suppgid1 = op_data->op_suppgids[0];
	rec->cr_suppgid2 = op_data->op_suppgids[1];
	flags = op_data->op_flags & MF_SOM_LOCAL_FLAGS;
	if (op_data->op_bias & MDS_CREATE_VOLATILE)
		flags |= MDS_OPEN_VOLATILE;
	set_mrc_cr_flags(rec, flags);
	rec->cr_bias     = op_data->op_bias;
	rec->cr_umask    = current_umask();

	mdc_pack_name(req, &RMF_NAME, op_data->op_name, op_data->op_namelen);
	if (data) {
		tmp = req_capsule_client_get(&req->rq_pill, &RMF_EADATA);
		memcpy(tmp, data, datalen);
	}
}

static __u64 mds_pack_open_flags(__u64 flags, __u32 mode)
{
	__u64 cr_flags = (flags & (FMODE_READ | FMODE_WRITE |
				   MDS_OPEN_FL_INTERNAL));
	if (flags & O_CREAT)
		cr_flags |= MDS_OPEN_CREAT;
	if (flags & O_EXCL)
		cr_flags |= MDS_OPEN_EXCL;
	if (flags & O_TRUNC)
		cr_flags |= MDS_OPEN_TRUNC;
	if (flags & O_APPEND)
		cr_flags |= MDS_OPEN_APPEND;
	if (flags & O_SYNC)
		cr_flags |= MDS_OPEN_SYNC;
	if (flags & O_DIRECTORY)
		cr_flags |= MDS_OPEN_DIRECTORY;
	if (flags & __FMODE_EXEC)
		cr_flags |= MDS_FMODE_EXEC;
	if (cl_is_lov_delay_create(flags))
		cr_flags |= MDS_OPEN_DELAY_CREATE;

	if (flags & O_NONBLOCK)
		cr_flags |= MDS_OPEN_NORESTORE;

	return cr_flags;
}

/* packing of MDS records */
void mdc_open_pack(struct ptlrpc_request *req, struct md_op_data *op_data,
		   __u32 mode, __u64 rdev, __u64 flags, const void *lmm,
		   int lmmlen)
{
	struct mdt_rec_create *rec;
	char *tmp;
	__u64 cr_flags;

	CLASSERT(sizeof(struct mdt_rec_reint) == sizeof(struct mdt_rec_create));
	rec = req_capsule_client_get(&req->rq_pill, &RMF_REC_REINT);

	/* XXX do something about time, uid, gid */
	rec->cr_opcode   = REINT_OPEN;
	rec->cr_fsuid    = from_kuid(&init_user_ns, current_fsuid());
	rec->cr_fsgid    = from_kgid(&init_user_ns, current_fsgid());
	rec->cr_cap      = cfs_curproc_cap_pack();
	rec->cr_fid1 = op_data->op_fid1;
	rec->cr_fid2 = op_data->op_fid2;

	rec->cr_mode     = mode;
	cr_flags = mds_pack_open_flags(flags, mode);
	rec->cr_rdev     = rdev;
	rec->cr_time     = op_data->op_mod_time;
	rec->cr_suppgid1 = op_data->op_suppgids[0];
	rec->cr_suppgid2 = op_data->op_suppgids[1];
	rec->cr_bias     = op_data->op_bias;
	rec->cr_umask    = current_umask();
	rec->cr_old_handle = op_data->op_handle;

	if (op_data->op_name) {
		mdc_pack_name(req, &RMF_NAME, op_data->op_name,
			      op_data->op_namelen);

		if (op_data->op_bias & MDS_CREATE_VOLATILE)
			cr_flags |= MDS_OPEN_VOLATILE;
	}

	if (lmm) {
		cr_flags |= MDS_OPEN_HAS_EA;
		tmp = req_capsule_client_get(&req->rq_pill, &RMF_EADATA);
		memcpy(tmp, lmm, lmmlen);
	}
	set_mrc_cr_flags(rec, cr_flags);
}

static inline __u64 attr_pack(unsigned int ia_valid)
{
	__u64 sa_valid = 0;

	if (ia_valid & ATTR_MODE)
		sa_valid |= MDS_ATTR_MODE;
	if (ia_valid & ATTR_UID)
		sa_valid |= MDS_ATTR_UID;
	if (ia_valid & ATTR_GID)
		sa_valid |= MDS_ATTR_GID;
	if (ia_valid & ATTR_SIZE)
		sa_valid |= MDS_ATTR_SIZE;
	if (ia_valid & ATTR_ATIME)
		sa_valid |= MDS_ATTR_ATIME;
	if (ia_valid & ATTR_MTIME)
		sa_valid |= MDS_ATTR_MTIME;
	if (ia_valid & ATTR_CTIME)
		sa_valid |= MDS_ATTR_CTIME;
	if (ia_valid & ATTR_ATIME_SET)
		sa_valid |= MDS_ATTR_ATIME_SET;
	if (ia_valid & ATTR_MTIME_SET)
		sa_valid |= MDS_ATTR_MTIME_SET;
	if (ia_valid & ATTR_FORCE)
		sa_valid |= MDS_ATTR_FORCE;
	if (ia_valid & ATTR_ATTR_FLAG)
		sa_valid |= MDS_ATTR_ATTR_FLAG;
	if (ia_valid & ATTR_KILL_SUID)
		sa_valid |=  MDS_ATTR_KILL_SUID;
	if (ia_valid & ATTR_KILL_SGID)
		sa_valid |= MDS_ATTR_KILL_SGID;
	if (ia_valid & ATTR_CTIME_SET)
		sa_valid |= MDS_ATTR_CTIME_SET;
	if (ia_valid & ATTR_OPEN)
		sa_valid |= MDS_ATTR_FROM_OPEN;
	if (ia_valid & ATTR_BLOCKS)
		sa_valid |= MDS_ATTR_BLOCKS;
	if (ia_valid & MDS_OPEN_OWNEROVERRIDE)
		/* NFSD hack (see bug 5781) */
		sa_valid |= MDS_OPEN_OWNEROVERRIDE;
	return sa_valid;
}

static void mdc_setattr_pack_rec(struct mdt_rec_setattr *rec,
				 struct md_op_data *op_data)
{
	rec->sa_opcode  = REINT_SETATTR;
	rec->sa_fsuid   = from_kuid(&init_user_ns, current_fsuid());
	rec->sa_fsgid   = from_kgid(&init_user_ns, current_fsgid());
	rec->sa_cap     = cfs_curproc_cap_pack();
	rec->sa_suppgid = -1;

	rec->sa_fid    = op_data->op_fid1;
	rec->sa_valid  = attr_pack(op_data->op_attr.ia_valid);
	rec->sa_mode   = op_data->op_attr.ia_mode;
	rec->sa_uid    = from_kuid(&init_user_ns, op_data->op_attr.ia_uid);
	rec->sa_gid    = from_kgid(&init_user_ns, op_data->op_attr.ia_gid);
	rec->sa_size   = op_data->op_attr.ia_size;
	rec->sa_blocks = op_data->op_attr_blocks;
	rec->sa_atime  = LTIME_S(op_data->op_attr.ia_atime);
	rec->sa_mtime  = LTIME_S(op_data->op_attr.ia_mtime);
	rec->sa_ctime  = LTIME_S(op_data->op_attr.ia_ctime);
	rec->sa_attr_flags = op_data->op_attr_flags;
	if ((op_data->op_attr.ia_valid & ATTR_GID) &&
	    in_group_p(op_data->op_attr.ia_gid))
		rec->sa_suppgid =
			from_kgid(&init_user_ns, op_data->op_attr.ia_gid);
	else
		rec->sa_suppgid = op_data->op_suppgids[0];

	rec->sa_bias = op_data->op_bias;
}

static void mdc_ioepoch_pack(struct mdt_ioepoch *epoch,
			     struct md_op_data *op_data)
{
	memcpy(&epoch->handle, &op_data->op_handle, sizeof(epoch->handle));
	epoch->ioepoch = op_data->op_ioepoch;
	epoch->flags = op_data->op_flags & MF_SOM_LOCAL_FLAGS;
}

void mdc_setattr_pack(struct ptlrpc_request *req, struct md_op_data *op_data,
		      void *ea, int ealen, void *ea2, int ea2len)
{
	struct mdt_rec_setattr *rec;
	struct mdt_ioepoch *epoch;
	struct lov_user_md *lum = NULL;

	CLASSERT(sizeof(struct mdt_rec_reint) ==
					sizeof(struct mdt_rec_setattr));
	rec = req_capsule_client_get(&req->rq_pill, &RMF_REC_REINT);
	mdc_setattr_pack_rec(rec, op_data);

	if (op_data->op_flags & (MF_SOM_CHANGE | MF_EPOCH_OPEN)) {
		epoch = req_capsule_client_get(&req->rq_pill, &RMF_MDT_EPOCH);
		mdc_ioepoch_pack(epoch, op_data);
	}

	if (ealen == 0)
		return;

	lum = req_capsule_client_get(&req->rq_pill, &RMF_EADATA);
	if (!ea) { /* Remove LOV EA */
		lum->lmm_magic = LOV_USER_MAGIC_V1;
		lum->lmm_stripe_size = 0;
		lum->lmm_stripe_count = 0;
		lum->lmm_stripe_offset = (typeof(lum->lmm_stripe_offset))(-1);
	} else {
		memcpy(lum, ea, ealen);
	}

	if (ea2len == 0)
		return;

	memcpy(req_capsule_client_get(&req->rq_pill, &RMF_LOGCOOKIES), ea2,
	       ea2len);
}

void mdc_unlink_pack(struct ptlrpc_request *req, struct md_op_data *op_data)
{
	struct mdt_rec_unlink *rec;

	CLASSERT(sizeof(struct mdt_rec_reint) == sizeof(struct mdt_rec_unlink));
	rec = req_capsule_client_get(&req->rq_pill, &RMF_REC_REINT);

	rec->ul_opcode   = op_data->op_cli_flags & CLI_RM_ENTRY ?
					REINT_RMENTRY : REINT_UNLINK;
	rec->ul_fsuid    = op_data->op_fsuid;
	rec->ul_fsgid    = op_data->op_fsgid;
	rec->ul_cap      = op_data->op_cap;
	rec->ul_mode     = op_data->op_mode;
	rec->ul_suppgid1 = op_data->op_suppgids[0];
	rec->ul_suppgid2 = -1;
	rec->ul_fid1     = op_data->op_fid1;
	rec->ul_fid2     = op_data->op_fid2;
	rec->ul_time     = op_data->op_mod_time;
	rec->ul_bias     = op_data->op_bias;

	mdc_pack_name(req, &RMF_NAME, op_data->op_name, op_data->op_namelen);
}

void mdc_link_pack(struct ptlrpc_request *req, struct md_op_data *op_data)
{
	struct mdt_rec_link *rec;

	CLASSERT(sizeof(struct mdt_rec_reint) == sizeof(struct mdt_rec_link));
	rec = req_capsule_client_get(&req->rq_pill, &RMF_REC_REINT);

	rec->lk_opcode   = REINT_LINK;
	rec->lk_fsuid    = op_data->op_fsuid; /* current->fsuid; */
	rec->lk_fsgid    = op_data->op_fsgid; /* current->fsgid; */
	rec->lk_cap      = op_data->op_cap;   /* current->cap_effective; */
	rec->lk_suppgid1 = op_data->op_suppgids[0];
	rec->lk_suppgid2 = op_data->op_suppgids[1];
	rec->lk_fid1     = op_data->op_fid1;
	rec->lk_fid2     = op_data->op_fid2;
	rec->lk_time     = op_data->op_mod_time;
	rec->lk_bias     = op_data->op_bias;

	mdc_pack_name(req, &RMF_NAME, op_data->op_name, op_data->op_namelen);
}

void mdc_rename_pack(struct ptlrpc_request *req, struct md_op_data *op_data,
		     const char *old, int oldlen, const char *new, int newlen)
{
	struct mdt_rec_rename *rec;

	CLASSERT(sizeof(struct mdt_rec_reint) == sizeof(struct mdt_rec_rename));
	rec = req_capsule_client_get(&req->rq_pill, &RMF_REC_REINT);

	/* XXX do something about time, uid, gid */
	rec->rn_opcode	 = op_data->op_cli_flags & CLI_MIGRATE ?
				REINT_MIGRATE : REINT_RENAME;
	rec->rn_opcode   = REINT_RENAME;
	rec->rn_fsuid    = op_data->op_fsuid;
	rec->rn_fsgid    = op_data->op_fsgid;
	rec->rn_cap      = op_data->op_cap;
	rec->rn_suppgid1 = op_data->op_suppgids[0];
	rec->rn_suppgid2 = op_data->op_suppgids[1];
	rec->rn_fid1     = op_data->op_fid1;
	rec->rn_fid2     = op_data->op_fid2;
	rec->rn_time     = op_data->op_mod_time;
	rec->rn_mode     = op_data->op_mode;
	rec->rn_bias     = op_data->op_bias;

	mdc_pack_name(req, &RMF_NAME, old, oldlen);

	if (new)
		mdc_pack_name(req, &RMF_SYMTGT, new, newlen);
}

void mdc_getattr_pack(struct ptlrpc_request *req, __u64 valid, int flags,
		      struct md_op_data *op_data, int ea_size)
{
	struct mdt_body *b = req_capsule_client_get(&req->rq_pill,
						    &RMF_MDT_BODY);

	b->mbo_valid = valid;
	if (op_data->op_bias & MDS_CHECK_SPLIT)
		b->mbo_valid |= OBD_MD_FLCKSPLIT;
	if (op_data->op_bias & MDS_CROSS_REF)
		b->mbo_valid |= OBD_MD_FLCROSSREF;
	b->mbo_eadatasize = ea_size;
	b->mbo_flags = flags;
	__mdc_pack_body(b, op_data->op_suppgids[0]);

	b->mbo_fid1 = op_data->op_fid1;
	b->mbo_fid2 = op_data->op_fid2;
	b->mbo_valid |= OBD_MD_FLID;

	if (op_data->op_name)
		mdc_pack_name(req, &RMF_NAME, op_data->op_name,
			      op_data->op_namelen);
}

static void mdc_hsm_release_pack(struct ptlrpc_request *req,
				 struct md_op_data *op_data)
{
	if (op_data->op_bias & MDS_HSM_RELEASE) {
		struct close_data *data;
		struct ldlm_lock *lock;

		data = req_capsule_client_get(&req->rq_pill, &RMF_CLOSE_DATA);

		lock = ldlm_handle2lock(&op_data->op_lease_handle);
		if (lock) {
			data->cd_handle = lock->l_remote_handle;
			LDLM_LOCK_PUT(lock);
		}
		ldlm_cli_cancel(&op_data->op_lease_handle, LCF_LOCAL);

		data->cd_data_version = op_data->op_data_version;
		data->cd_fid = op_data->op_fid2;
	}
}

void mdc_close_pack(struct ptlrpc_request *req, struct md_op_data *op_data)
{
	struct mdt_ioepoch *epoch;
	struct mdt_rec_setattr *rec;

	epoch = req_capsule_client_get(&req->rq_pill, &RMF_MDT_EPOCH);
	rec = req_capsule_client_get(&req->rq_pill, &RMF_REC_REINT);

	mdc_setattr_pack_rec(rec, op_data);
	/*
	 * The client will zero out local timestamps when losing the IBITS lock
	 * so any new RPC timestamps will update the client inode's timestamps.
	 * There was a defect on the server side which allowed the atime to be
	 * overwritten by a zeroed-out atime packed into the close RPC.
	 *
	 * Proactively clear the MDS_ATTR_ATIME flag in the RPC in this case
	 * to avoid zeroing the atime on old unpatched servers.  See LU-8041.
	 */
	if (rec->sa_atime == 0)
		rec->sa_valid &= ~MDS_ATTR_ATIME;

	mdc_ioepoch_pack(epoch, op_data);
	mdc_hsm_release_pack(req, op_data);
}
