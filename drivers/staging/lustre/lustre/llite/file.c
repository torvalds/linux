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
 *
 * Copyright (c) 2011, 2015, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lustre/llite/file.c
 *
 * Author: Peter Braam <braam@clusterfs.com>
 * Author: Phil Schwan <phil@clusterfs.com>
 * Author: Andreas Dilger <adilger@clusterfs.com>
 */

#define DEBUG_SUBSYSTEM S_LLITE
#include "../include/lustre_dlm.h"
#include <linux/pagemap.h>
#include <linux/file.h>
#include <linux/sched.h>
#include <linux/mount.h>
#include "../include/lustre/ll_fiemap.h"
#include "../include/lustre/lustre_ioctl.h"

#include "../include/cl_object.h"
#include "llite_internal.h"

static int
ll_put_grouplock(struct inode *inode, struct file *file, unsigned long arg);

static int ll_lease_close(struct obd_client_handle *och, struct inode *inode,
			  bool *lease_broken);

static enum llioc_iter
ll_iocontrol_call(struct inode *inode, struct file *file,
		  unsigned int cmd, unsigned long arg, int *rcp);

static struct ll_file_data *ll_file_data_get(void)
{
	struct ll_file_data *fd;

	fd = kmem_cache_zalloc(ll_file_data_slab, GFP_NOFS);
	if (!fd)
		return NULL;
	fd->fd_write_failed = false;
	return fd;
}

static void ll_file_data_put(struct ll_file_data *fd)
{
	if (fd)
		kmem_cache_free(ll_file_data_slab, fd);
}

void ll_pack_inode2opdata(struct inode *inode, struct md_op_data *op_data,
			  struct lustre_handle *fh)
{
	op_data->op_fid1 = ll_i2info(inode)->lli_fid;
	op_data->op_attr.ia_mode = inode->i_mode;
	op_data->op_attr.ia_atime = inode->i_atime;
	op_data->op_attr.ia_mtime = inode->i_mtime;
	op_data->op_attr.ia_ctime = inode->i_ctime;
	op_data->op_attr.ia_size = i_size_read(inode);
	op_data->op_attr_blocks = inode->i_blocks;
	op_data->op_attr_flags = ll_inode_to_ext_flags(inode->i_flags);
	if (fh)
		op_data->op_handle = *fh;

	if (ll_i2info(inode)->lli_flags & LLIF_DATA_MODIFIED)
		op_data->op_bias |= MDS_DATA_MODIFIED;
}

/**
 * Packs all the attributes into @op_data for the CLOSE rpc.
 */
static void ll_prepare_close(struct inode *inode, struct md_op_data *op_data,
			     struct obd_client_handle *och)
{
	op_data->op_attr.ia_valid = ATTR_MODE | ATTR_ATIME | ATTR_ATIME_SET |
					ATTR_MTIME | ATTR_MTIME_SET |
					ATTR_CTIME | ATTR_CTIME_SET;

	if (!(och->och_flags & FMODE_WRITE))
		goto out;

	op_data->op_attr.ia_valid |= ATTR_SIZE | ATTR_BLOCKS;
out:
	ll_pack_inode2opdata(inode, op_data, &och->och_fh);
	ll_prep_md_op_data(op_data, inode, NULL, NULL,
			   0, 0, LUSTRE_OPC_ANY, NULL);
}

/**
 * Perform a close, possibly with a bias.
 * The meaning of "data" depends on the value of "bias".
 *
 * If \a bias is MDS_HSM_RELEASE then \a data is a pointer to the data version.
 * If \a bias is MDS_CLOSE_LAYOUT_SWAP then \a data is a pointer to the inode to
 * swap layouts with.
 */
static int ll_close_inode_openhandle(struct obd_export *md_exp,
				     struct obd_client_handle *och,
				     struct inode *inode,
				     enum mds_op_bias bias,
				     void *data)
{
	struct obd_export *exp = ll_i2mdexp(inode);
	struct md_op_data *op_data;
	struct ptlrpc_request *req = NULL;
	struct obd_device *obd = class_exp2obd(exp);
	int rc;

	if (!obd) {
		/*
		 * XXX: in case of LMV, is this correct to access
		 * ->exp_handle?
		 */
		CERROR("Invalid MDC connection handle %#llx\n",
		       ll_i2mdexp(inode)->exp_handle.h_cookie);
		rc = 0;
		goto out;
	}

	op_data = kzalloc(sizeof(*op_data), GFP_NOFS);
	if (!op_data) {
		/* XXX We leak openhandle and request here. */
		rc = -ENOMEM;
		goto out;
	}

	ll_prepare_close(inode, op_data, och);
	switch (bias) {
	case MDS_CLOSE_LAYOUT_SWAP:
		LASSERT(data);
		op_data->op_bias |= MDS_CLOSE_LAYOUT_SWAP;
		op_data->op_data_version = 0;
		op_data->op_lease_handle = och->och_lease_handle;
		op_data->op_fid2 = *ll_inode2fid(data);
		break;

	case MDS_HSM_RELEASE:
		LASSERT(data);
		op_data->op_bias |= MDS_HSM_RELEASE;
		op_data->op_data_version = *(__u64 *)data;
		op_data->op_lease_handle = och->och_lease_handle;
		op_data->op_attr.ia_valid |= ATTR_SIZE | ATTR_BLOCKS;
		break;

	default:
		LASSERT(!data);
		break;
	}

	rc = md_close(md_exp, op_data, och->och_mod, &req);
	if (rc) {
		CERROR("%s: inode "DFID" mdc close failed: rc = %d\n",
		       ll_i2mdexp(inode)->exp_obd->obd_name,
		       PFID(ll_inode2fid(inode)), rc);
	}

	/* DATA_MODIFIED flag was successfully sent on close, cancel data
	 * modification flag.
	 */
	if (rc == 0 && (op_data->op_bias & MDS_DATA_MODIFIED)) {
		struct ll_inode_info *lli = ll_i2info(inode);

		spin_lock(&lli->lli_lock);
		lli->lli_flags &= ~LLIF_DATA_MODIFIED;
		spin_unlock(&lli->lli_lock);
	}

	if (op_data->op_bias & (MDS_HSM_RELEASE | MDS_CLOSE_LAYOUT_SWAP) &&
	    !rc) {
		struct mdt_body *body;

		body = req_capsule_server_get(&req->rq_pill, &RMF_MDT_BODY);
		if (!(body->mbo_valid & OBD_MD_CLOSE_INTENT_EXECED))
			rc = -EBUSY;
	}

	ll_finish_md_op_data(op_data);

out:
	md_clear_open_replay_data(md_exp, och);
	och->och_fh.cookie = DEAD_HANDLE_MAGIC;
	kfree(och);

	if (req) /* This is close request */
		ptlrpc_req_finished(req);
	return rc;
}

int ll_md_real_close(struct inode *inode, fmode_t fmode)
{
	struct ll_inode_info *lli = ll_i2info(inode);
	struct obd_client_handle **och_p;
	struct obd_client_handle *och;
	__u64 *och_usecount;
	int rc = 0;

	if (fmode & FMODE_WRITE) {
		och_p = &lli->lli_mds_write_och;
		och_usecount = &lli->lli_open_fd_write_count;
	} else if (fmode & FMODE_EXEC) {
		och_p = &lli->lli_mds_exec_och;
		och_usecount = &lli->lli_open_fd_exec_count;
	} else {
		LASSERT(fmode & FMODE_READ);
		och_p = &lli->lli_mds_read_och;
		och_usecount = &lli->lli_open_fd_read_count;
	}

	mutex_lock(&lli->lli_och_mutex);
	if (*och_usecount > 0) {
		/* There are still users of this handle, so skip
		 * freeing it.
		 */
		mutex_unlock(&lli->lli_och_mutex);
		return 0;
	}

	och = *och_p;
	*och_p = NULL;
	mutex_unlock(&lli->lli_och_mutex);

	if (och) {
		/* There might be a race and this handle may already
		 * be closed.
		 */
		rc = ll_close_inode_openhandle(ll_i2sbi(inode)->ll_md_exp,
					       och, inode, 0, NULL);
	}

	return rc;
}

static int ll_md_close(struct obd_export *md_exp, struct inode *inode,
		       struct file *file)
{
	struct ll_file_data *fd = LUSTRE_FPRIVATE(file);
	struct ll_inode_info *lli = ll_i2info(inode);
	int lockmode;
	__u64 flags = LDLM_FL_BLOCK_GRANTED | LDLM_FL_TEST_LOCK;
	struct lustre_handle lockh;
	ldlm_policy_data_t policy = {.l_inodebits = {MDS_INODELOCK_OPEN} };
	int rc = 0;

	/* clear group lock, if present */
	if (unlikely(fd->fd_flags & LL_FILE_GROUP_LOCKED))
		ll_put_grouplock(inode, file, fd->fd_grouplock.lg_gid);

	if (fd->fd_lease_och) {
		bool lease_broken;

		/* Usually the lease is not released when the
		 * application crashed, we need to release here.
		 */
		rc = ll_lease_close(fd->fd_lease_och, inode, &lease_broken);
		CDEBUG(rc ? D_ERROR : D_INODE,
		       "Clean up lease " DFID " %d/%d\n",
		       PFID(&lli->lli_fid), rc, lease_broken);

		fd->fd_lease_och = NULL;
	}

	if (fd->fd_och) {
		rc = ll_close_inode_openhandle(md_exp, fd->fd_och, inode, 0,
					       NULL);
		fd->fd_och = NULL;
		goto out;
	}

	/* Let's see if we have good enough OPEN lock on the file and if
	 * we can skip talking to MDS
	 */

	mutex_lock(&lli->lli_och_mutex);
	if (fd->fd_omode & FMODE_WRITE) {
		lockmode = LCK_CW;
		LASSERT(lli->lli_open_fd_write_count);
		lli->lli_open_fd_write_count--;
	} else if (fd->fd_omode & FMODE_EXEC) {
		lockmode = LCK_PR;
		LASSERT(lli->lli_open_fd_exec_count);
		lli->lli_open_fd_exec_count--;
	} else {
		lockmode = LCK_CR;
		LASSERT(lli->lli_open_fd_read_count);
		lli->lli_open_fd_read_count--;
	}
	mutex_unlock(&lli->lli_och_mutex);

	if (!md_lock_match(md_exp, flags, ll_inode2fid(inode),
			   LDLM_IBITS, &policy, lockmode, &lockh))
		rc = ll_md_real_close(inode, fd->fd_omode);

out:
	LUSTRE_FPRIVATE(file) = NULL;
	ll_file_data_put(fd);

	return rc;
}

/* While this returns an error code, fput() the caller does not, so we need
 * to make every effort to clean up all of our state here.  Also, applications
 * rarely check close errors and even if an error is returned they will not
 * re-try the close call.
 */
int ll_file_release(struct inode *inode, struct file *file)
{
	struct ll_file_data *fd;
	struct ll_sb_info *sbi = ll_i2sbi(inode);
	struct ll_inode_info *lli = ll_i2info(inode);
	int rc;

	CDEBUG(D_VFSTRACE, "VFS Op:inode="DFID"(%p)\n",
	       PFID(ll_inode2fid(inode)), inode);

	if (!is_root_inode(inode))
		ll_stats_ops_tally(sbi, LPROC_LL_RELEASE, 1);
	fd = LUSTRE_FPRIVATE(file);
	LASSERT(fd);

	/* The last ref on @file, maybe not be the owner pid of statahead,
	 * because parent and child process can share the same file handle.
	 */
	if (S_ISDIR(inode->i_mode) && lli->lli_opendir_key == fd)
		ll_deauthorize_statahead(inode, fd);

	if (is_root_inode(inode)) {
		LUSTRE_FPRIVATE(file) = NULL;
		ll_file_data_put(fd);
		return 0;
	}

	if (!S_ISDIR(inode->i_mode)) {
		if (lli->lli_clob)
			lov_read_and_clear_async_rc(lli->lli_clob);
		lli->lli_async_rc = 0;
	}

	rc = ll_md_close(sbi->ll_md_exp, inode, file);

	if (CFS_FAIL_TIMEOUT_MS(OBD_FAIL_PTLRPC_DUMP_LOG, cfs_fail_val))
		libcfs_debug_dumplog();

	return rc;
}

static int ll_intent_file_open(struct dentry *de, void *lmm, int lmmsize,
			       struct lookup_intent *itp)
{
	struct inode *inode = d_inode(de);
	struct ll_sb_info *sbi = ll_i2sbi(inode);
	struct dentry *parent = de->d_parent;
	const char *name = NULL;
	struct md_op_data *op_data;
	struct ptlrpc_request *req = NULL;
	int len = 0, rc;

	LASSERT(parent);
	LASSERT(itp->it_flags & MDS_OPEN_BY_FID);

	/*
	 * if server supports open-by-fid, or file name is invalid, don't pack
	 * name in open request
	 */
	if (!(exp_connect_flags(sbi->ll_md_exp) & OBD_CONNECT_OPEN_BY_FID) &&
	    lu_name_is_valid_2(de->d_name.name, de->d_name.len)) {
		name = de->d_name.name;
		len = de->d_name.len;
	}

	op_data  = ll_prep_md_op_data(NULL, d_inode(parent), inode, name, len,
				      O_RDWR, LUSTRE_OPC_ANY, NULL);
	if (IS_ERR(op_data))
		return PTR_ERR(op_data);
	op_data->op_data = lmm;
	op_data->op_data_size = lmmsize;

	rc = md_intent_lock(sbi->ll_md_exp, op_data, itp, &req,
			    &ll_md_blocking_ast, 0);
	ll_finish_md_op_data(op_data);
	if (rc == -ESTALE) {
		/* reason for keep own exit path - don`t flood log
		* with messages with -ESTALE errors.
		*/
		if (!it_disposition(itp, DISP_OPEN_OPEN) ||
		    it_open_error(DISP_OPEN_OPEN, itp))
			goto out;
		ll_release_openhandle(inode, itp);
		goto out;
	}

	if (it_disposition(itp, DISP_LOOKUP_NEG)) {
		rc = -ENOENT;
		goto out;
	}

	if (rc != 0 || it_open_error(DISP_OPEN_OPEN, itp)) {
		rc = rc ? rc : it_open_error(DISP_OPEN_OPEN, itp);
		CDEBUG(D_VFSTRACE, "lock enqueue: err: %d\n", rc);
		goto out;
	}

	rc = ll_prep_inode(&inode, req, NULL, itp);
	if (!rc && itp->it_lock_mode)
		ll_set_lock_data(sbi->ll_md_exp, inode, itp, NULL);

out:
	ptlrpc_req_finished(req);
	ll_intent_drop_lock(itp);

	return rc;
}

static int ll_och_fill(struct obd_export *md_exp, struct lookup_intent *it,
		       struct obd_client_handle *och)
{
	struct mdt_body *body;

	body = req_capsule_server_get(&it->it_request->rq_pill, &RMF_MDT_BODY);
	och->och_fh = body->mbo_handle;
	och->och_fid = body->mbo_fid1;
	och->och_lease_handle.cookie = it->it_lock_handle;
	och->och_magic = OBD_CLIENT_HANDLE_MAGIC;
	och->och_flags = it->it_flags;

	return md_set_open_replay_data(md_exp, och, it);
}

static int ll_local_open(struct file *file, struct lookup_intent *it,
			 struct ll_file_data *fd, struct obd_client_handle *och)
{
	struct inode *inode = file_inode(file);

	LASSERT(!LUSTRE_FPRIVATE(file));

	LASSERT(fd);

	if (och) {
		int rc;

		rc = ll_och_fill(ll_i2sbi(inode)->ll_md_exp, it, och);
		if (rc != 0)
			return rc;
	}

	LUSTRE_FPRIVATE(file) = fd;
	ll_readahead_init(inode, &fd->fd_ras);
	fd->fd_omode = it->it_flags & (FMODE_READ | FMODE_WRITE | FMODE_EXEC);

	/* ll_cl_context initialize */
	rwlock_init(&fd->fd_lock);
	INIT_LIST_HEAD(&fd->fd_lccs);

	return 0;
}

/* Open a file, and (for the very first open) create objects on the OSTs at
 * this time.  If opened with O_LOV_DELAY_CREATE, then we don't do the object
 * creation or open until ll_lov_setstripe() ioctl is called.
 *
 * If we already have the stripe MD locally then we don't request it in
 * md_open(), by passing a lmm_size = 0.
 *
 * It is up to the application to ensure no other processes open this file
 * in the O_LOV_DELAY_CREATE case, or the default striping pattern will be
 * used.  We might be able to avoid races of that sort by getting lli_open_sem
 * before returning in the O_LOV_DELAY_CREATE case and dropping it here
 * or in ll_file_release(), but I'm not sure that is desirable/necessary.
 */
int ll_file_open(struct inode *inode, struct file *file)
{
	struct ll_inode_info *lli = ll_i2info(inode);
	struct lookup_intent *it, oit = { .it_op = IT_OPEN,
					  .it_flags = file->f_flags };
	struct obd_client_handle **och_p = NULL;
	__u64 *och_usecount = NULL;
	struct ll_file_data *fd;
	int rc = 0;

	CDEBUG(D_VFSTRACE, "VFS Op:inode="DFID"(%p), flags %o\n",
	       PFID(ll_inode2fid(inode)), inode, file->f_flags);

	it = file->private_data; /* XXX: compat macro */
	file->private_data = NULL; /* prevent ll_local_open assertion */

	fd = ll_file_data_get();
	if (!fd) {
		rc = -ENOMEM;
		goto out_openerr;
	}

	fd->fd_file = file;
	if (S_ISDIR(inode->i_mode))
		ll_authorize_statahead(inode, fd);

	if (is_root_inode(inode)) {
		LUSTRE_FPRIVATE(file) = fd;
		return 0;
	}

	if (!it || !it->it_disposition) {
		/* Convert f_flags into access mode. We cannot use file->f_mode,
		 * because everything but O_ACCMODE mask was stripped from
		 * there
		 */
		if ((oit.it_flags + 1) & O_ACCMODE)
			oit.it_flags++;
		if (file->f_flags & O_TRUNC)
			oit.it_flags |= FMODE_WRITE;

		/* kernel only call f_op->open in dentry_open.  filp_open calls
		 * dentry_open after call to open_namei that checks permissions.
		 * Only nfsd_open call dentry_open directly without checking
		 * permissions and because of that this code below is safe.
		 */
		if (oit.it_flags & (FMODE_WRITE | FMODE_READ))
			oit.it_flags |= MDS_OPEN_OWNEROVERRIDE;

		/* We do not want O_EXCL here, presumably we opened the file
		 * already? XXX - NFS implications?
		 */
		oit.it_flags &= ~O_EXCL;

		/* bug20584, if "it_flags" contains O_CREAT, the file will be
		 * created if necessary, then "IT_CREAT" should be set to keep
		 * consistent with it
		 */
		if (oit.it_flags & O_CREAT)
			oit.it_op |= IT_CREAT;

		it = &oit;
	}

restart:
	/* Let's see if we have file open on MDS already. */
	if (it->it_flags & FMODE_WRITE) {
		och_p = &lli->lli_mds_write_och;
		och_usecount = &lli->lli_open_fd_write_count;
	} else if (it->it_flags & FMODE_EXEC) {
		och_p = &lli->lli_mds_exec_och;
		och_usecount = &lli->lli_open_fd_exec_count;
	} else {
		och_p = &lli->lli_mds_read_och;
		och_usecount = &lli->lli_open_fd_read_count;
	}

	mutex_lock(&lli->lli_och_mutex);
	if (*och_p) { /* Open handle is present */
		if (it_disposition(it, DISP_OPEN_OPEN)) {
			/* Well, there's extra open request that we do not need,
			 * let's close it somehow. This will decref request.
			 */
			rc = it_open_error(DISP_OPEN_OPEN, it);
			if (rc) {
				mutex_unlock(&lli->lli_och_mutex);
				goto out_openerr;
			}

			ll_release_openhandle(inode, it);
		}
		(*och_usecount)++;

		rc = ll_local_open(file, it, fd, NULL);
		if (rc) {
			(*och_usecount)--;
			mutex_unlock(&lli->lli_och_mutex);
			goto out_openerr;
		}
	} else {
		LASSERT(*och_usecount == 0);
		if (!it->it_disposition) {
			/* We cannot just request lock handle now, new ELC code
			 * means that one of other OPEN locks for this file
			 * could be cancelled, and since blocking ast handler
			 * would attempt to grab och_mutex as well, that would
			 * result in a deadlock
			 */
			mutex_unlock(&lli->lli_och_mutex);
			/*
			 * Normally called under two situations:
			 * 1. NFS export.
			 * 2. revalidate with IT_OPEN (revalidate doesn't
			 *    execute this intent any more).
			 *
			 * Always fetch MDS_OPEN_LOCK if this is not setstripe.
			 *
			 * Always specify MDS_OPEN_BY_FID because we don't want
			 * to get file with different fid.
			 */
			it->it_flags |= MDS_OPEN_LOCK | MDS_OPEN_BY_FID;
			rc = ll_intent_file_open(file->f_path.dentry, NULL, 0, it);
			if (rc)
				goto out_openerr;

			goto restart;
		}
		*och_p = kzalloc(sizeof(struct obd_client_handle), GFP_NOFS);
		if (!*och_p) {
			rc = -ENOMEM;
			goto out_och_free;
		}

		(*och_usecount)++;

		/* md_intent_lock() didn't get a request ref if there was an
		 * open error, so don't do cleanup on the request here
		 * (bug 3430)
		 */
		/* XXX (green): Should not we bail out on any error here, not
		 * just open error?
		 */
		rc = it_open_error(DISP_OPEN_OPEN, it);
		if (rc)
			goto out_och_free;

		LASSERTF(it_disposition(it, DISP_ENQ_OPEN_REF),
			 "inode %p: disposition %x, status %d\n", inode,
			 it_disposition(it, ~0), it->it_status);

		rc = ll_local_open(file, it, fd, *och_p);
		if (rc)
			goto out_och_free;
	}
	mutex_unlock(&lli->lli_och_mutex);
	fd = NULL;

	/* Must do this outside lli_och_mutex lock to prevent deadlock where
	 * different kind of OPEN lock for this same inode gets cancelled
	 * by ldlm_cancel_lru
	 */
	if (!S_ISREG(inode->i_mode))
		goto out_och_free;

	cl_lov_delay_create_clear(&file->f_flags);
	goto out_och_free;

out_och_free:
	if (rc) {
		if (och_p && *och_p) {
			kfree(*och_p);
			*och_p = NULL;
			(*och_usecount)--;
		}
		mutex_unlock(&lli->lli_och_mutex);

out_openerr:
		if (lli->lli_opendir_key == fd)
			ll_deauthorize_statahead(inode, fd);
		if (fd)
			ll_file_data_put(fd);
	} else {
		ll_stats_ops_tally(ll_i2sbi(inode), LPROC_LL_OPEN, 1);
	}

	if (it && it_disposition(it, DISP_ENQ_OPEN_REF)) {
		ptlrpc_req_finished(it->it_request);
		it_clear_disposition(it, DISP_ENQ_OPEN_REF);
	}

	return rc;
}

static int ll_md_blocking_lease_ast(struct ldlm_lock *lock,
				    struct ldlm_lock_desc *desc,
				    void *data, int flag)
{
	int rc;
	struct lustre_handle lockh;

	switch (flag) {
	case LDLM_CB_BLOCKING:
		ldlm_lock2handle(lock, &lockh);
		rc = ldlm_cli_cancel(&lockh, LCF_ASYNC);
		if (rc < 0) {
			CDEBUG(D_INODE, "ldlm_cli_cancel: %d\n", rc);
			return rc;
		}
		break;
	case LDLM_CB_CANCELING:
		/* do nothing */
		break;
	}
	return 0;
}

/**
 * Acquire a lease and open the file.
 */
static struct obd_client_handle *
ll_lease_open(struct inode *inode, struct file *file, fmode_t fmode,
	      __u64 open_flags)
{
	struct lookup_intent it = { .it_op = IT_OPEN };
	struct ll_sb_info *sbi = ll_i2sbi(inode);
	struct md_op_data *op_data;
	struct ptlrpc_request *req = NULL;
	struct lustre_handle old_handle = { 0 };
	struct obd_client_handle *och = NULL;
	int rc;
	int rc2;

	if (fmode != FMODE_WRITE && fmode != FMODE_READ)
		return ERR_PTR(-EINVAL);

	if (file) {
		struct ll_inode_info *lli = ll_i2info(inode);
		struct ll_file_data *fd = LUSTRE_FPRIVATE(file);
		struct obd_client_handle **och_p;
		__u64 *och_usecount;

		if (!(fmode & file->f_mode) || (file->f_mode & FMODE_EXEC))
			return ERR_PTR(-EPERM);

		/* Get the openhandle of the file */
		rc = -EBUSY;
		mutex_lock(&lli->lli_och_mutex);
		if (fd->fd_lease_och) {
			mutex_unlock(&lli->lli_och_mutex);
			return ERR_PTR(rc);
		}

		if (!fd->fd_och) {
			if (file->f_mode & FMODE_WRITE) {
				LASSERT(lli->lli_mds_write_och);
				och_p = &lli->lli_mds_write_och;
				och_usecount = &lli->lli_open_fd_write_count;
			} else {
				LASSERT(lli->lli_mds_read_och);
				och_p = &lli->lli_mds_read_och;
				och_usecount = &lli->lli_open_fd_read_count;
			}
			if (*och_usecount == 1) {
				fd->fd_och = *och_p;
				*och_p = NULL;
				*och_usecount = 0;
				rc = 0;
			}
		}
		mutex_unlock(&lli->lli_och_mutex);
		if (rc < 0) /* more than 1 opener */
			return ERR_PTR(rc);

		LASSERT(fd->fd_och);
		old_handle = fd->fd_och->och_fh;
	}

	och = kzalloc(sizeof(*och), GFP_NOFS);
	if (!och)
		return ERR_PTR(-ENOMEM);

	op_data = ll_prep_md_op_data(NULL, inode, inode, NULL, 0, 0,
				     LUSTRE_OPC_ANY, NULL);
	if (IS_ERR(op_data)) {
		rc = PTR_ERR(op_data);
		goto out;
	}

	/* To tell the MDT this openhandle is from the same owner */
	op_data->op_handle = old_handle;

	it.it_flags = fmode | open_flags;
	it.it_flags |= MDS_OPEN_LOCK | MDS_OPEN_BY_FID | MDS_OPEN_LEASE;
	rc = md_intent_lock(sbi->ll_md_exp, op_data, &it, &req,
			    &ll_md_blocking_lease_ast,
	/* LDLM_FL_NO_LRU: To not put the lease lock into LRU list, otherwise
	 * it can be cancelled which may mislead applications that the lease is
	 * broken;
	 * LDLM_FL_EXCL: Set this flag so that it won't be matched by normal
	 * open in ll_md_blocking_ast(). Otherwise as ll_md_blocking_lease_ast
	 * doesn't deal with openhandle, so normal openhandle will be leaked.
	 */
			    LDLM_FL_NO_LRU | LDLM_FL_EXCL);
	ll_finish_md_op_data(op_data);
	ptlrpc_req_finished(req);
	if (rc < 0)
		goto out_release_it;

	if (it_disposition(&it, DISP_LOOKUP_NEG)) {
		rc = -ENOENT;
		goto out_release_it;
	}

	rc = it_open_error(DISP_OPEN_OPEN, &it);
	if (rc)
		goto out_release_it;

	LASSERT(it_disposition(&it, DISP_ENQ_OPEN_REF));
	ll_och_fill(sbi->ll_md_exp, &it, och);

	if (!it_disposition(&it, DISP_OPEN_LEASE)) /* old server? */ {
		rc = -EOPNOTSUPP;
		goto out_close;
	}

	/* already get lease, handle lease lock */
	ll_set_lock_data(sbi->ll_md_exp, inode, &it, NULL);
	if (it.it_lock_mode == 0 ||
	    it.it_lock_bits != MDS_INODELOCK_OPEN) {
		/* open lock must return for lease */
		CERROR(DFID "lease granted but no open lock, %d/%llu.\n",
		       PFID(ll_inode2fid(inode)), it.it_lock_mode,
		       it.it_lock_bits);
		rc = -EPROTO;
		goto out_close;
	}

	ll_intent_release(&it);
	return och;

out_close:
	/* Cancel open lock */
	if (it.it_lock_mode != 0) {
		ldlm_lock_decref_and_cancel(&och->och_lease_handle,
					    it.it_lock_mode);
		it.it_lock_mode = 0;
		och->och_lease_handle.cookie = 0ULL;
	}
	rc2 = ll_close_inode_openhandle(sbi->ll_md_exp, och, inode, 0, NULL);
	if (rc2 < 0)
		CERROR("%s: error closing file "DFID": %d\n",
		       ll_get_fsname(inode->i_sb, NULL, 0),
		       PFID(&ll_i2info(inode)->lli_fid), rc2);
	och = NULL; /* och has been freed in ll_close_inode_openhandle() */
out_release_it:
	ll_intent_release(&it);
out:
	kfree(och);
	return ERR_PTR(rc);
}

/**
 * Check whether a layout swap can be done between two inodes.
 *
 * \param[in] inode1  First inode to check
 * \param[in] inode2  Second inode to check
 *
 * \retval 0 on success, layout swap can be performed between both inodes
 * \retval negative error code if requirements are not met
 */
static int ll_check_swap_layouts_validity(struct inode *inode1,
					  struct inode *inode2)
{
	if (!S_ISREG(inode1->i_mode) || !S_ISREG(inode2->i_mode))
		return -EINVAL;

	if (inode_permission(inode1, MAY_WRITE) ||
	    inode_permission(inode2, MAY_WRITE))
		return -EPERM;

	if (inode1->i_sb != inode2->i_sb)
		return -EXDEV;

	return 0;
}

static int ll_swap_layouts_close(struct obd_client_handle *och,
				 struct inode *inode, struct inode *inode2)
{
	const struct lu_fid *fid1 = ll_inode2fid(inode);
	const struct lu_fid *fid2;
	int rc;

	CDEBUG(D_INODE, "%s: biased close of file " DFID "\n",
	       ll_get_fsname(inode->i_sb, NULL, 0), PFID(fid1));

	rc = ll_check_swap_layouts_validity(inode, inode2);
	if (rc < 0)
		goto out_free_och;

	/* We now know that inode2 is a lustre inode */
	fid2 = ll_inode2fid(inode2);

	rc = lu_fid_cmp(fid1, fid2);
	if (!rc) {
		rc = -EINVAL;
		goto out_free_och;
	}

	/*
	 * Close the file and swap layouts between inode & inode2.
	 * NB: lease lock handle is released in mdc_close_layout_swap_pack()
	 * because we still need it to pack l_remote_handle to MDT.
	 */
	rc = ll_close_inode_openhandle(ll_i2sbi(inode)->ll_md_exp, och, inode,
				       MDS_CLOSE_LAYOUT_SWAP, inode2);

	och = NULL; /* freed in ll_close_inode_openhandle() */

out_free_och:
	kfree(och);
	return rc;
}

/**
 * Release lease and close the file.
 * It will check if the lease has ever broken.
 */
static int ll_lease_close(struct obd_client_handle *och, struct inode *inode,
			  bool *lease_broken)
{
	struct ldlm_lock *lock;
	bool cancelled = true;

	lock = ldlm_handle2lock(&och->och_lease_handle);
	if (lock) {
		lock_res_and_lock(lock);
		cancelled = ldlm_is_cancel(lock);
		unlock_res_and_lock(lock);
		LDLM_LOCK_PUT(lock);
	}

	CDEBUG(D_INODE, "lease for " DFID " broken? %d\n",
	       PFID(&ll_i2info(inode)->lli_fid), cancelled);

	if (!cancelled)
		ldlm_cli_cancel(&och->och_lease_handle, 0);
	if (lease_broken)
		*lease_broken = cancelled;

	return ll_close_inode_openhandle(ll_i2sbi(inode)->ll_md_exp,
					 och, inode, 0, NULL);
}

int ll_merge_attr(const struct lu_env *env, struct inode *inode)
{
	struct ll_inode_info *lli = ll_i2info(inode);
	struct cl_object *obj = lli->lli_clob;
	struct cl_attr *attr = vvp_env_thread_attr(env);
	s64 atime;
	s64 mtime;
	s64 ctime;
	int rc = 0;

	ll_inode_size_lock(inode);

	/* merge timestamps the most recently obtained from mds with
	 * timestamps obtained from osts
	 */
	LTIME_S(inode->i_atime) = lli->lli_atime;
	LTIME_S(inode->i_mtime) = lli->lli_mtime;
	LTIME_S(inode->i_ctime) = lli->lli_ctime;

	mtime = LTIME_S(inode->i_mtime);
	atime = LTIME_S(inode->i_atime);
	ctime = LTIME_S(inode->i_ctime);

	cl_object_attr_lock(obj);
	rc = cl_object_attr_get(env, obj, attr);
	cl_object_attr_unlock(obj);

	if (rc != 0)
		goto out_size_unlock;

	if (atime < attr->cat_atime)
		atime = attr->cat_atime;

	if (ctime < attr->cat_ctime)
		ctime = attr->cat_ctime;

	if (mtime < attr->cat_mtime)
		mtime = attr->cat_mtime;

	CDEBUG(D_VFSTRACE, DFID " updating i_size %llu\n",
	       PFID(&lli->lli_fid), attr->cat_size);

	i_size_write(inode, attr->cat_size);

	inode->i_blocks = attr->cat_blocks;

	LTIME_S(inode->i_mtime) = mtime;
	LTIME_S(inode->i_atime) = atime;
	LTIME_S(inode->i_ctime) = ctime;

out_size_unlock:
	ll_inode_size_unlock(inode);

	return rc;
}

static bool file_is_noatime(const struct file *file)
{
	const struct vfsmount *mnt = file->f_path.mnt;
	const struct inode *inode = file_inode(file);

	/* Adapted from file_accessed() and touch_atime().*/
	if (file->f_flags & O_NOATIME)
		return true;

	if (inode->i_flags & S_NOATIME)
		return true;

	if (IS_NOATIME(inode))
		return true;

	if (mnt->mnt_flags & (MNT_NOATIME | MNT_READONLY))
		return true;

	if ((mnt->mnt_flags & MNT_NODIRATIME) && S_ISDIR(inode->i_mode))
		return true;

	if ((inode->i_sb->s_flags & MS_NODIRATIME) && S_ISDIR(inode->i_mode))
		return true;

	return false;
}

void ll_io_init(struct cl_io *io, const struct file *file, int write)
{
	struct inode *inode = file_inode(file);

	io->u.ci_rw.crw_nonblock = file->f_flags & O_NONBLOCK;
	if (write) {
		io->u.ci_wr.wr_append = !!(file->f_flags & O_APPEND);
		io->u.ci_wr.wr_sync = file->f_flags & O_SYNC ||
				      file->f_flags & O_DIRECT ||
				      IS_SYNC(inode);
	}
	io->ci_obj     = ll_i2info(inode)->lli_clob;
	io->ci_lockreq = CILR_MAYBE;
	if (ll_file_nolock(file)) {
		io->ci_lockreq = CILR_NEVER;
		io->ci_no_srvlock = 1;
	} else if (file->f_flags & O_APPEND) {
		io->ci_lockreq = CILR_MANDATORY;
	}

	io->ci_noatime = file_is_noatime(file);
}

static ssize_t
ll_file_io_generic(const struct lu_env *env, struct vvp_io_args *args,
		   struct file *file, enum cl_io_type iot,
		   loff_t *ppos, size_t count)
{
	struct ll_inode_info *lli = ll_i2info(file_inode(file));
	struct ll_file_data  *fd  = LUSTRE_FPRIVATE(file);
	struct vvp_io *vio = vvp_env_io(env);
	struct range_lock range;
	struct cl_io	 *io;
	ssize_t result = 0;
	int rc = 0;

	CDEBUG(D_VFSTRACE, "file: %pD, type: %d ppos: %llu, count: %zu\n",
	       file, iot, *ppos, count);

restart:
	io = vvp_env_thread_io(env);
	ll_io_init(io, file, iot == CIT_WRITE);

	if (cl_io_rw_init(env, io, iot, *ppos, count) == 0) {
		struct vvp_io *vio = vvp_env_io(env);
		bool range_locked = false;

		if (file->f_flags & O_APPEND)
			range_lock_init(&range, 0, LUSTRE_EOF);
		else
			range_lock_init(&range, *ppos, *ppos + count - 1);

		vio->vui_fd  = LUSTRE_FPRIVATE(file);
		vio->vui_iter = args->u.normal.via_iter;
		vio->vui_iocb = args->u.normal.via_iocb;
		/*
		 * Direct IO reads must also take range lock,
		 * or multiple reads will try to work on the same pages
		 * See LU-6227 for details.
		 */
		if (((iot == CIT_WRITE) ||
		     (iot == CIT_READ && (file->f_flags & O_DIRECT))) &&
		    !(vio->vui_fd->fd_flags & LL_FILE_GROUP_LOCKED)) {
			CDEBUG(D_VFSTRACE, "Range lock [%llu, %llu]\n",
			       range.rl_node.in_extent.start,
			       range.rl_node.in_extent.end);
			rc = range_lock(&lli->lli_write_tree, &range);
			if (rc < 0)
				goto out;

			range_locked = true;
		}
		ll_cl_add(file, env, io);
		rc = cl_io_loop(env, io);
		ll_cl_remove(file, env);
		if (range_locked) {
			CDEBUG(D_VFSTRACE, "Range unlock [%llu, %llu]\n",
			       range.rl_node.in_extent.start,
			       range.rl_node.in_extent.end);
			range_unlock(&lli->lli_write_tree, &range);
		}
	} else {
		/* cl_io_rw_init() handled IO */
		rc = io->ci_result;
	}

	if (io->ci_nob > 0) {
		result = io->ci_nob;
		count -= io->ci_nob;
		*ppos = io->u.ci_wr.wr.crw_pos;

		/* prepare IO restart */
		if (count > 0)
			args->u.normal.via_iter = vio->vui_iter;
	}
out:
	cl_io_fini(env, io);

	if ((!rc || rc == -ENODATA) && count > 0 && io->ci_need_restart) {
		CDEBUG(D_VFSTRACE, "%s: restart %s from %lld, count:%zu, result: %zd\n",
		       file_dentry(file)->d_name.name,
		       iot == CIT_READ ? "read" : "write",
		       *ppos, count, result);
		goto restart;
	}

	if (iot == CIT_READ) {
		if (result >= 0)
			ll_stats_ops_tally(ll_i2sbi(file_inode(file)),
					   LPROC_LL_READ_BYTES, result);
	} else if (iot == CIT_WRITE) {
		if (result >= 0) {
			ll_stats_ops_tally(ll_i2sbi(file_inode(file)),
					   LPROC_LL_WRITE_BYTES, result);
			fd->fd_write_failed = false;
		} else if (!result && !rc) {
			rc = io->ci_result;
			if (rc < 0)
				fd->fd_write_failed = true;
			else
				fd->fd_write_failed = false;
		} else if (rc != -ERESTARTSYS) {
			fd->fd_write_failed = true;
		}
	}
	CDEBUG(D_VFSTRACE, "iot: %d, result: %zd\n", iot, result);

	return result > 0 ? result : rc;
}

static ssize_t ll_file_read_iter(struct kiocb *iocb, struct iov_iter *to)
{
	struct lu_env      *env;
	struct vvp_io_args *args;
	ssize_t	     result;
	int		 refcheck;

	env = cl_env_get(&refcheck);
	if (IS_ERR(env))
		return PTR_ERR(env);

	args = ll_env_args(env);
	args->u.normal.via_iter = to;
	args->u.normal.via_iocb = iocb;

	result = ll_file_io_generic(env, args, iocb->ki_filp, CIT_READ,
				    &iocb->ki_pos, iov_iter_count(to));
	cl_env_put(env, &refcheck);
	return result;
}

/*
 * Write to a file (through the page cache).
 */
static ssize_t ll_file_write_iter(struct kiocb *iocb, struct iov_iter *from)
{
	struct lu_env      *env;
	struct vvp_io_args *args;
	ssize_t	     result;
	int		 refcheck;

	env = cl_env_get(&refcheck);
	if (IS_ERR(env))
		return PTR_ERR(env);

	args = ll_env_args(env);
	args->u.normal.via_iter = from;
	args->u.normal.via_iocb = iocb;

	result = ll_file_io_generic(env, args, iocb->ki_filp, CIT_WRITE,
				    &iocb->ki_pos, iov_iter_count(from));
	cl_env_put(env, &refcheck);
	return result;
}

int ll_lov_setstripe_ea_info(struct inode *inode, struct dentry *dentry,
			     __u64 flags, struct lov_user_md *lum,
			     int lum_size)
{
	struct lookup_intent oit = {
		.it_op = IT_OPEN,
		.it_flags = flags | MDS_OPEN_BY_FID,
	};
	int rc = 0;

	ll_inode_size_lock(inode);
	rc = ll_intent_file_open(dentry, lum, lum_size, &oit);
	if (rc < 0)
		goto out_unlock;

	ll_release_openhandle(inode, &oit);

out_unlock:
	ll_inode_size_unlock(inode);
	ll_intent_release(&oit);
	return rc;
}

int ll_lov_getstripe_ea_info(struct inode *inode, const char *filename,
			     struct lov_mds_md **lmmp, int *lmm_size,
			     struct ptlrpc_request **request)
{
	struct ll_sb_info *sbi = ll_i2sbi(inode);
	struct mdt_body  *body;
	struct lov_mds_md *lmm = NULL;
	struct ptlrpc_request *req = NULL;
	struct md_op_data *op_data;
	int rc, lmmsize;

	rc = ll_get_default_mdsize(sbi, &lmmsize);
	if (rc)
		return rc;

	op_data = ll_prep_md_op_data(NULL, inode, NULL, filename,
				     strlen(filename), lmmsize,
				     LUSTRE_OPC_ANY, NULL);
	if (IS_ERR(op_data))
		return PTR_ERR(op_data);

	op_data->op_valid = OBD_MD_FLEASIZE | OBD_MD_FLDIREA;
	rc = md_getattr_name(sbi->ll_md_exp, op_data, &req);
	ll_finish_md_op_data(op_data);
	if (rc < 0) {
		CDEBUG(D_INFO, "md_getattr_name failed on %s: rc %d\n",
		       filename, rc);
		goto out;
	}

	body = req_capsule_server_get(&req->rq_pill, &RMF_MDT_BODY);

	lmmsize = body->mbo_eadatasize;

	if (!(body->mbo_valid & (OBD_MD_FLEASIZE | OBD_MD_FLDIREA)) ||
	    lmmsize == 0) {
		rc = -ENODATA;
		goto out;
	}

	lmm = req_capsule_server_sized_get(&req->rq_pill, &RMF_MDT_MD, lmmsize);

	if ((lmm->lmm_magic != cpu_to_le32(LOV_MAGIC_V1)) &&
	    (lmm->lmm_magic != cpu_to_le32(LOV_MAGIC_V3))) {
		rc = -EPROTO;
		goto out;
	}

	/*
	 * This is coming from the MDS, so is probably in
	 * little endian.  We convert it to host endian before
	 * passing it to userspace.
	 */
	if (cpu_to_le32(LOV_MAGIC) != LOV_MAGIC) {
		int stripe_count;

		stripe_count = le16_to_cpu(lmm->lmm_stripe_count);
		if (le32_to_cpu(lmm->lmm_pattern) & LOV_PATTERN_F_RELEASED)
			stripe_count = 0;

		/* if function called for directory - we should
		 * avoid swab not existent lsm objects
		 */
		if (lmm->lmm_magic == cpu_to_le32(LOV_MAGIC_V1)) {
			lustre_swab_lov_user_md_v1((struct lov_user_md_v1 *)lmm);
			if (S_ISREG(body->mbo_mode))
				lustre_swab_lov_user_md_objects(
				 ((struct lov_user_md_v1 *)lmm)->lmm_objects,
				 stripe_count);
		} else if (lmm->lmm_magic == cpu_to_le32(LOV_MAGIC_V3)) {
			lustre_swab_lov_user_md_v3((struct lov_user_md_v3 *)lmm);
			if (S_ISREG(body->mbo_mode))
				lustre_swab_lov_user_md_objects(
				 ((struct lov_user_md_v3 *)lmm)->lmm_objects,
				 stripe_count);
		}
	}

out:
	*lmmp = lmm;
	*lmm_size = lmmsize;
	*request = req;
	return rc;
}

static int ll_lov_setea(struct inode *inode, struct file *file,
			unsigned long arg)
{
	__u64			 flags = MDS_OPEN_HAS_OBJS | FMODE_WRITE;
	struct lov_user_md	*lump;
	int			 lum_size = sizeof(struct lov_user_md) +
					    sizeof(struct lov_user_ost_data);
	int			 rc;

	if (!capable(CFS_CAP_SYS_ADMIN))
		return -EPERM;

	lump = libcfs_kvzalloc(lum_size, GFP_NOFS);
	if (!lump)
		return -ENOMEM;

	if (copy_from_user(lump, (struct lov_user_md __user *)arg, lum_size)) {
		kvfree(lump);
		return -EFAULT;
	}

	rc = ll_lov_setstripe_ea_info(inode, file->f_path.dentry, flags, lump,
				      lum_size);
	cl_lov_delay_create_clear(&file->f_flags);

	kvfree(lump);
	return rc;
}

static int ll_file_getstripe(struct inode *inode,
			     struct lov_user_md __user *lum)
{
	struct lu_env *env;
	int refcheck;
	int rc;

	env = cl_env_get(&refcheck);
	if (IS_ERR(env))
		return PTR_ERR(env);

	rc = cl_object_getstripe(env, ll_i2info(inode)->lli_clob, lum);
	cl_env_put(env, &refcheck);
	return rc;
}

static int ll_lov_setstripe(struct inode *inode, struct file *file,
			    unsigned long arg)
{
	struct lov_user_md __user *lum = (struct lov_user_md __user *)arg;
	struct lov_user_md *klum;
	int lum_size, rc;
	__u64 flags = FMODE_WRITE;

	rc = ll_copy_user_md(lum, &klum);
	if (rc < 0)
		return rc;

	lum_size = rc;
	rc = ll_lov_setstripe_ea_info(inode, file->f_path.dentry, flags, klum,
				      lum_size);
	cl_lov_delay_create_clear(&file->f_flags);
	if (rc == 0) {
		__u32 gen;

		put_user(0, &lum->lmm_stripe_count);

		ll_layout_refresh(inode, &gen);
		rc = ll_file_getstripe(inode, (struct lov_user_md __user *)arg);
	}

	kfree(klum);
	return rc;
}

static int
ll_get_grouplock(struct inode *inode, struct file *file, unsigned long arg)
{
	struct ll_inode_info   *lli = ll_i2info(inode);
	struct ll_file_data    *fd = LUSTRE_FPRIVATE(file);
	struct ll_grouplock    grouplock;
	int		     rc;

	if (arg == 0) {
		CWARN("group id for group lock must not be 0\n");
		return -EINVAL;
	}

	if (ll_file_nolock(file))
		return -EOPNOTSUPP;

	spin_lock(&lli->lli_lock);
	if (fd->fd_flags & LL_FILE_GROUP_LOCKED) {
		CWARN("group lock already existed with gid %lu\n",
		      fd->fd_grouplock.lg_gid);
		spin_unlock(&lli->lli_lock);
		return -EINVAL;
	}
	LASSERT(!fd->fd_grouplock.lg_lock);
	spin_unlock(&lli->lli_lock);

	rc = cl_get_grouplock(ll_i2info(inode)->lli_clob,
			      arg, (file->f_flags & O_NONBLOCK), &grouplock);
	if (rc)
		return rc;

	spin_lock(&lli->lli_lock);
	if (fd->fd_flags & LL_FILE_GROUP_LOCKED) {
		spin_unlock(&lli->lli_lock);
		CERROR("another thread just won the race\n");
		cl_put_grouplock(&grouplock);
		return -EINVAL;
	}

	fd->fd_flags |= LL_FILE_GROUP_LOCKED;
	fd->fd_grouplock = grouplock;
	spin_unlock(&lli->lli_lock);

	CDEBUG(D_INFO, "group lock %lu obtained\n", arg);
	return 0;
}

static int ll_put_grouplock(struct inode *inode, struct file *file,
			    unsigned long arg)
{
	struct ll_inode_info   *lli = ll_i2info(inode);
	struct ll_file_data    *fd = LUSTRE_FPRIVATE(file);
	struct ll_grouplock    grouplock;

	spin_lock(&lli->lli_lock);
	if (!(fd->fd_flags & LL_FILE_GROUP_LOCKED)) {
		spin_unlock(&lli->lli_lock);
		CWARN("no group lock held\n");
		return -EINVAL;
	}
	LASSERT(fd->fd_grouplock.lg_lock);

	if (fd->fd_grouplock.lg_gid != arg) {
		CWARN("group lock %lu doesn't match current id %lu\n",
		      arg, fd->fd_grouplock.lg_gid);
		spin_unlock(&lli->lli_lock);
		return -EINVAL;
	}

	grouplock = fd->fd_grouplock;
	memset(&fd->fd_grouplock, 0, sizeof(fd->fd_grouplock));
	fd->fd_flags &= ~LL_FILE_GROUP_LOCKED;
	spin_unlock(&lli->lli_lock);

	cl_put_grouplock(&grouplock);
	CDEBUG(D_INFO, "group lock %lu released\n", arg);
	return 0;
}

/**
 * Close inode open handle
 *
 * \param inode  [in]     inode in question
 * \param it     [in,out] intent which contains open info and result
 *
 * \retval 0     success
 * \retval <0    failure
 */
int ll_release_openhandle(struct inode *inode, struct lookup_intent *it)
{
	struct obd_client_handle *och;
	int rc;

	LASSERT(inode);

	/* Root ? Do nothing. */
	if (is_root_inode(inode))
		return 0;

	/* No open handle to close? Move away */
	if (!it_disposition(it, DISP_OPEN_OPEN))
		return 0;

	LASSERT(it_open_error(DISP_OPEN_OPEN, it) == 0);

	och = kzalloc(sizeof(*och), GFP_NOFS);
	if (!och) {
		rc = -ENOMEM;
		goto out;
	}

	ll_och_fill(ll_i2sbi(inode)->ll_md_exp, it, och);

	rc = ll_close_inode_openhandle(ll_i2sbi(inode)->ll_md_exp,
				       och, inode, 0, NULL);
out:
	/* this one is in place of ll_file_open */
	if (it_disposition(it, DISP_ENQ_OPEN_REF)) {
		ptlrpc_req_finished(it->it_request);
		it_clear_disposition(it, DISP_ENQ_OPEN_REF);
	}
	return rc;
}

/**
 * Get size for inode for which FIEMAP mapping is requested.
 * Make the FIEMAP get_info call and returns the result.
 *
 * \param fiemap	kernel buffer to hold extens
 * \param num_bytes	kernel buffer size
 */
static int ll_do_fiemap(struct inode *inode, struct fiemap *fiemap,
			size_t num_bytes)
{
	struct ll_fiemap_info_key fmkey = { .lfik_name = KEY_FIEMAP, };
	struct lu_env *env;
	int refcheck;
	int rc = 0;

	/* Checks for fiemap flags */
	if (fiemap->fm_flags & ~LUSTRE_FIEMAP_FLAGS_COMPAT) {
		fiemap->fm_flags &= ~LUSTRE_FIEMAP_FLAGS_COMPAT;
		return -EBADR;
	}

	/* Check for FIEMAP_FLAG_SYNC */
	if (fiemap->fm_flags & FIEMAP_FLAG_SYNC) {
		rc = filemap_fdatawrite(inode->i_mapping);
		if (rc)
			return rc;
	}

	env = cl_env_get(&refcheck);
	if (IS_ERR(env))
		return PTR_ERR(env);

	if (i_size_read(inode) == 0) {
		rc = ll_glimpse_size(inode);
		if (rc)
			goto out;
	}

	fmkey.lfik_oa.o_valid = OBD_MD_FLID | OBD_MD_FLGROUP;
	obdo_from_inode(&fmkey.lfik_oa, inode, OBD_MD_FLSIZE);
	obdo_set_parent_fid(&fmkey.lfik_oa, &ll_i2info(inode)->lli_fid);

	/* If filesize is 0, then there would be no objects for mapping */
	if (fmkey.lfik_oa.o_size == 0) {
		fiemap->fm_mapped_extents = 0;
		rc = 0;
		goto out;
	}

	memcpy(&fmkey.lfik_fiemap, fiemap, sizeof(*fiemap));

	rc = cl_object_fiemap(env, ll_i2info(inode)->lli_clob,
			      &fmkey, fiemap, &num_bytes);
out:
	cl_env_put(env, &refcheck);
	return rc;
}

int ll_fid2path(struct inode *inode, void __user *arg)
{
	struct obd_export *exp = ll_i2mdexp(inode);
	const struct getinfo_fid2path __user *gfin = arg;
	struct getinfo_fid2path *gfout;
	u32 pathlen;
	size_t outsize;
	int rc;

	if (!capable(CFS_CAP_DAC_READ_SEARCH) &&
	    !(ll_i2sbi(inode)->ll_flags & LL_SBI_USER_FID2PATH))
		return -EPERM;

	/* Only need to get the buflen */
	if (get_user(pathlen, &gfin->gf_pathlen))
		return -EFAULT;

	if (pathlen > PATH_MAX)
		return -EINVAL;

	outsize = sizeof(*gfout) + pathlen;

	gfout = kzalloc(outsize, GFP_NOFS);
	if (!gfout)
		return -ENOMEM;

	if (copy_from_user(gfout, arg, sizeof(*gfout))) {
		rc = -EFAULT;
		goto gf_free;
	}

	/* Call mdc_iocontrol */
	rc = obd_iocontrol(OBD_IOC_FID2PATH, exp, outsize, gfout, NULL);
	if (rc != 0)
		goto gf_free;

	if (copy_to_user(arg, gfout, outsize))
		rc = -EFAULT;

gf_free:
	kfree(gfout);
	return rc;
}

/*
 * Read the data_version for inode.
 *
 * This value is computed using stripe object version on OST.
 * Version is computed using server side locking.
 *
 * @param flags if do sync on the OST side;
 *		0: no sync
 *		LL_DV_RD_FLUSH: flush dirty pages, LCK_PR on OSTs
 *		LL_DV_WR_FLUSH: drop all caching pages, LCK_PW on OSTs
 */
int ll_data_version(struct inode *inode, __u64 *data_version, int flags)
{
	struct cl_object *obj = ll_i2info(inode)->lli_clob;
	struct lu_env *env;
	struct cl_io *io;
	int refcheck;
	int result;

	/* If no file object initialized, we consider its version is 0. */
	if (!obj) {
		*data_version = 0;
		return 0;
	}

	env = cl_env_get(&refcheck);
	if (IS_ERR(env))
		return PTR_ERR(env);

	io = vvp_env_thread_io(env);
	io->ci_obj = obj;
	io->u.ci_data_version.dv_data_version = 0;
	io->u.ci_data_version.dv_flags = flags;

restart:
	if (!cl_io_init(env, io, CIT_DATA_VERSION, io->ci_obj))
		result = cl_io_loop(env, io);
	else
		result = io->ci_result;

	*data_version = io->u.ci_data_version.dv_data_version;

	cl_io_fini(env, io);

	if (unlikely(io->ci_need_restart))
		goto restart;

	cl_env_put(env, &refcheck);

	return result;
}

/*
 * Trigger a HSM release request for the provided inode.
 */
int ll_hsm_release(struct inode *inode)
{
	struct lu_env *env;
	struct obd_client_handle *och = NULL;
	__u64 data_version = 0;
	int rc;
	int refcheck;

	CDEBUG(D_INODE, "%s: Releasing file "DFID".\n",
	       ll_get_fsname(inode->i_sb, NULL, 0),
	       PFID(&ll_i2info(inode)->lli_fid));

	och = ll_lease_open(inode, NULL, FMODE_WRITE, MDS_OPEN_RELEASE);
	if (IS_ERR(och)) {
		rc = PTR_ERR(och);
		goto out;
	}

	/* Grab latest data_version and [am]time values */
	rc = ll_data_version(inode, &data_version, LL_DV_WR_FLUSH);
	if (rc != 0)
		goto out;

	env = cl_env_get(&refcheck);
	if (IS_ERR(env)) {
		rc = PTR_ERR(env);
		goto out;
	}

	ll_merge_attr(env, inode);
	cl_env_put(env, &refcheck);

	/* Release the file.
	 * NB: lease lock handle is released in mdc_hsm_release_pack() because
	 * we still need it to pack l_remote_handle to MDT.
	 */
	rc = ll_close_inode_openhandle(ll_i2sbi(inode)->ll_md_exp, och, inode,
				       MDS_HSM_RELEASE, &data_version);
	och = NULL;

out:
	if (och && !IS_ERR(och)) /* close the file */
		ll_lease_close(och, inode, NULL);

	return rc;
}

struct ll_swap_stack {
	u64		dv1;
	u64		dv2;
	struct inode   *inode1;
	struct inode   *inode2;
	bool		check_dv1;
	bool		check_dv2;
};

static int ll_swap_layouts(struct file *file1, struct file *file2,
			   struct lustre_swap_layouts *lsl)
{
	struct mdc_swap_layouts	 msl;
	struct md_op_data	*op_data;
	__u32			 gid;
	__u64			 dv;
	struct ll_swap_stack	*llss = NULL;
	int			 rc;

	llss = kzalloc(sizeof(*llss), GFP_NOFS);
	if (!llss)
		return -ENOMEM;

	llss->inode1 = file_inode(file1);
	llss->inode2 = file_inode(file2);

	rc = ll_check_swap_layouts_validity(llss->inode1, llss->inode2);
	if (rc < 0)
		goto free;

	/* we use 2 bool because it is easier to swap than 2 bits */
	if (lsl->sl_flags & SWAP_LAYOUTS_CHECK_DV1)
		llss->check_dv1 = true;

	if (lsl->sl_flags & SWAP_LAYOUTS_CHECK_DV2)
		llss->check_dv2 = true;

	/* we cannot use lsl->sl_dvX directly because we may swap them */
	llss->dv1 = lsl->sl_dv1;
	llss->dv2 = lsl->sl_dv2;

	rc = lu_fid_cmp(ll_inode2fid(llss->inode1), ll_inode2fid(llss->inode2));
	if (!rc) /* same file, done! */
		goto free;

	if (rc < 0) { /* sequentialize it */
		swap(llss->inode1, llss->inode2);
		swap(file1, file2);
		swap(llss->dv1, llss->dv2);
		swap(llss->check_dv1, llss->check_dv2);
	}

	gid = lsl->sl_gid;
	if (gid != 0) { /* application asks to flush dirty cache */
		rc = ll_get_grouplock(llss->inode1, file1, gid);
		if (rc < 0)
			goto free;

		rc = ll_get_grouplock(llss->inode2, file2, gid);
		if (rc < 0) {
			ll_put_grouplock(llss->inode1, file1, gid);
			goto free;
		}
	}

	/* ultimate check, before swapping the layouts we check if
	 * dataversion has changed (if requested)
	 */
	if (llss->check_dv1) {
		rc = ll_data_version(llss->inode1, &dv, 0);
		if (rc)
			goto putgl;
		if (dv != llss->dv1) {
			rc = -EAGAIN;
			goto putgl;
		}
	}

	if (llss->check_dv2) {
		rc = ll_data_version(llss->inode2, &dv, 0);
		if (rc)
			goto putgl;
		if (dv != llss->dv2) {
			rc = -EAGAIN;
			goto putgl;
		}
	}

	/* struct md_op_data is used to send the swap args to the mdt
	 * only flags is missing, so we use struct mdc_swap_layouts
	 * through the md_op_data->op_data
	 */
	/* flags from user space have to be converted before they are send to
	 * server, no flag is sent today, they are only used on the client
	 */
	msl.msl_flags = 0;
	rc = -ENOMEM;
	op_data = ll_prep_md_op_data(NULL, llss->inode1, llss->inode2, NULL, 0,
				     0, LUSTRE_OPC_ANY, &msl);
	if (IS_ERR(op_data)) {
		rc = PTR_ERR(op_data);
		goto free;
	}

	rc = obd_iocontrol(LL_IOC_LOV_SWAP_LAYOUTS, ll_i2mdexp(llss->inode1),
			   sizeof(*op_data), op_data, NULL);
	ll_finish_md_op_data(op_data);

putgl:
	if (gid != 0) {
		ll_put_grouplock(llss->inode2, file2, gid);
		ll_put_grouplock(llss->inode1, file1, gid);
	}

free:
	kfree(llss);

	return rc;
}

static int ll_hsm_state_set(struct inode *inode, struct hsm_state_set *hss)
{
	struct md_op_data	*op_data;
	int			 rc;

	/* Detect out-of range masks */
	if ((hss->hss_setmask | hss->hss_clearmask) & ~HSM_FLAGS_MASK)
		return -EINVAL;

	/* Non-root users are forbidden to set or clear flags which are
	 * NOT defined in HSM_USER_MASK.
	 */
	if (((hss->hss_setmask | hss->hss_clearmask) & ~HSM_USER_MASK) &&
	    !capable(CFS_CAP_SYS_ADMIN))
		return -EPERM;

	/* Detect out-of range archive id */
	if ((hss->hss_valid & HSS_ARCHIVE_ID) &&
	    (hss->hss_archive_id > LL_HSM_MAX_ARCHIVE))
		return -EINVAL;

	op_data = ll_prep_md_op_data(NULL, inode, NULL, NULL, 0, 0,
				     LUSTRE_OPC_ANY, hss);
	if (IS_ERR(op_data))
		return PTR_ERR(op_data);

	rc = obd_iocontrol(LL_IOC_HSM_STATE_SET, ll_i2mdexp(inode),
			   sizeof(*op_data), op_data, NULL);

	ll_finish_md_op_data(op_data);

	return rc;
}

static int ll_hsm_import(struct inode *inode, struct file *file,
			 struct hsm_user_import *hui)
{
	struct hsm_state_set	*hss = NULL;
	struct iattr		*attr = NULL;
	int			 rc;

	if (!S_ISREG(inode->i_mode))
		return -EINVAL;

	/* set HSM flags */
	hss = kzalloc(sizeof(*hss), GFP_NOFS);
	if (!hss)
		return -ENOMEM;

	hss->hss_valid = HSS_SETMASK | HSS_ARCHIVE_ID;
	hss->hss_archive_id = hui->hui_archive_id;
	hss->hss_setmask = HS_ARCHIVED | HS_EXISTS | HS_RELEASED;
	rc = ll_hsm_state_set(inode, hss);
	if (rc != 0)
		goto free_hss;

	attr = kzalloc(sizeof(*attr), GFP_NOFS);
	if (!attr) {
		rc = -ENOMEM;
		goto free_hss;
	}

	attr->ia_mode = hui->hui_mode & (S_IRWXU | S_IRWXG | S_IRWXO);
	attr->ia_mode |= S_IFREG;
	attr->ia_uid = make_kuid(&init_user_ns, hui->hui_uid);
	attr->ia_gid = make_kgid(&init_user_ns, hui->hui_gid);
	attr->ia_size = hui->hui_size;
	attr->ia_mtime.tv_sec = hui->hui_mtime;
	attr->ia_mtime.tv_nsec = hui->hui_mtime_ns;
	attr->ia_atime.tv_sec = hui->hui_atime;
	attr->ia_atime.tv_nsec = hui->hui_atime_ns;

	attr->ia_valid = ATTR_SIZE | ATTR_MODE | ATTR_FORCE |
			 ATTR_UID | ATTR_GID |
			 ATTR_MTIME | ATTR_MTIME_SET |
			 ATTR_ATIME | ATTR_ATIME_SET;

	inode_lock(inode);

	rc = ll_setattr_raw(file->f_path.dentry, attr, true);
	if (rc == -ENODATA)
		rc = 0;

	inode_unlock(inode);

	kfree(attr);
free_hss:
	kfree(hss);
	return rc;
}

static inline long ll_lease_type_from_fmode(fmode_t fmode)
{
	return ((fmode & FMODE_READ) ? LL_LEASE_RDLCK : 0) |
	       ((fmode & FMODE_WRITE) ? LL_LEASE_WRLCK : 0);
}

static long
ll_file_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct inode		*inode = file_inode(file);
	struct ll_file_data	*fd = LUSTRE_FPRIVATE(file);
	int			 flags, rc;

	CDEBUG(D_VFSTRACE, "VFS Op:inode="DFID"(%p),cmd=%x\n",
	       PFID(ll_inode2fid(inode)), inode, cmd);
	ll_stats_ops_tally(ll_i2sbi(inode), LPROC_LL_IOCTL, 1);

	/* asm-ppc{,64} declares TCGETS, et. al. as type 't' not 'T' */
	if (_IOC_TYPE(cmd) == 'T' || _IOC_TYPE(cmd) == 't') /* tty ioctls */
		return -ENOTTY;

	switch (cmd) {
	case LL_IOC_GETFLAGS:
		/* Get the current value of the file flags */
		return put_user(fd->fd_flags, (int __user *)arg);
	case LL_IOC_SETFLAGS:
	case LL_IOC_CLRFLAGS:
		/* Set or clear specific file flags */
		/* XXX This probably needs checks to ensure the flags are
		 *     not abused, and to handle any flag side effects.
		 */
		if (get_user(flags, (int __user *)arg))
			return -EFAULT;

		if (cmd == LL_IOC_SETFLAGS) {
			if ((flags & LL_FILE_IGNORE_LOCK) &&
			    !(file->f_flags & O_DIRECT)) {
				CERROR("%s: unable to disable locking on non-O_DIRECT file\n",
				       current->comm);
				return -EINVAL;
			}

			fd->fd_flags |= flags;
		} else {
			fd->fd_flags &= ~flags;
		}
		return 0;
	case LL_IOC_LOV_SETSTRIPE:
		return ll_lov_setstripe(inode, file, arg);
	case LL_IOC_LOV_SETEA:
		return ll_lov_setea(inode, file, arg);
	case LL_IOC_LOV_SWAP_LAYOUTS: {
		struct file *file2;
		struct lustre_swap_layouts lsl;

		if (copy_from_user(&lsl, (char __user *)arg,
				   sizeof(struct lustre_swap_layouts)))
			return -EFAULT;

		if ((file->f_flags & O_ACCMODE) == O_RDONLY)
			return -EPERM;

		file2 = fget(lsl.sl_fd);
		if (!file2)
			return -EBADF;

		/* O_WRONLY or O_RDWR */
		if ((file2->f_flags & O_ACCMODE) == O_RDONLY) {
			rc = -EPERM;
			goto out;
		}

		if (lsl.sl_flags & SWAP_LAYOUTS_CLOSE) {
			struct obd_client_handle *och = NULL;
			struct ll_inode_info *lli;
			struct inode *inode2;

			if (lsl.sl_flags != SWAP_LAYOUTS_CLOSE) {
				rc = -EINVAL;
				goto out;
			}

			lli = ll_i2info(inode);
			mutex_lock(&lli->lli_och_mutex);
			if (fd->fd_lease_och) {
				och = fd->fd_lease_och;
				fd->fd_lease_och = NULL;
			}
			mutex_unlock(&lli->lli_och_mutex);
			if (!och) {
				rc = -ENOLCK;
				goto out;
			}
			inode2 = file_inode(file2);
			rc = ll_swap_layouts_close(och, inode, inode2);
		} else {
			rc = ll_swap_layouts(file, file2, &lsl);
		}
out:
		fput(file2);
		return rc;
	}
	case LL_IOC_LOV_GETSTRIPE:
		return ll_file_getstripe(inode,
					 (struct lov_user_md __user *)arg);
	case FSFILT_IOC_GETFLAGS:
	case FSFILT_IOC_SETFLAGS:
		return ll_iocontrol(inode, file, cmd, arg);
	case FSFILT_IOC_GETVERSION_OLD:
	case FSFILT_IOC_GETVERSION:
		return put_user(inode->i_generation, (int __user *)arg);
	case LL_IOC_GROUP_LOCK:
		return ll_get_grouplock(inode, file, arg);
	case LL_IOC_GROUP_UNLOCK:
		return ll_put_grouplock(inode, file, arg);
	case IOC_OBD_STATFS:
		return ll_obd_statfs(inode, (void __user *)arg);

	/* We need to special case any other ioctls we want to handle,
	 * to send them to the MDS/OST as appropriate and to properly
	 * network encode the arg field.
	case FSFILT_IOC_SETVERSION_OLD:
	case FSFILT_IOC_SETVERSION:
	*/
	case LL_IOC_FLUSHCTX:
		return ll_flush_ctx(inode);
	case LL_IOC_PATH2FID: {
		if (copy_to_user((void __user *)arg, ll_inode2fid(inode),
				 sizeof(struct lu_fid)))
			return -EFAULT;

		return 0;
	}
	case LL_IOC_GETPARENT:
		return ll_getparent(file, (struct getparent __user *)arg);
	case OBD_IOC_FID2PATH:
		return ll_fid2path(inode, (void __user *)arg);
	case LL_IOC_DATA_VERSION: {
		struct ioc_data_version	idv;
		int			rc;

		if (copy_from_user(&idv, (char __user *)arg, sizeof(idv)))
			return -EFAULT;

		idv.idv_flags &= LL_DV_RD_FLUSH | LL_DV_WR_FLUSH;
		rc = ll_data_version(inode, &idv.idv_version, idv.idv_flags);
		if (rc == 0 && copy_to_user((char __user *)arg, &idv,
					    sizeof(idv)))
			return -EFAULT;

		return rc;
	}

	case LL_IOC_GET_MDTIDX: {
		int mdtidx;

		mdtidx = ll_get_mdt_idx(inode);
		if (mdtidx < 0)
			return mdtidx;

		if (put_user(mdtidx, (int __user *)arg))
			return -EFAULT;

		return 0;
	}
	case OBD_IOC_GETDTNAME:
	case OBD_IOC_GETMDNAME:
		return ll_get_obd_name(inode, cmd, arg);
	case LL_IOC_HSM_STATE_GET: {
		struct md_op_data	*op_data;
		struct hsm_user_state	*hus;
		int			 rc;

		hus = kzalloc(sizeof(*hus), GFP_NOFS);
		if (!hus)
			return -ENOMEM;

		op_data = ll_prep_md_op_data(NULL, inode, NULL, NULL, 0, 0,
					     LUSTRE_OPC_ANY, hus);
		if (IS_ERR(op_data)) {
			kfree(hus);
			return PTR_ERR(op_data);
		}

		rc = obd_iocontrol(cmd, ll_i2mdexp(inode), sizeof(*op_data),
				   op_data, NULL);

		if (copy_to_user((void __user *)arg, hus, sizeof(*hus)))
			rc = -EFAULT;

		ll_finish_md_op_data(op_data);
		kfree(hus);
		return rc;
	}
	case LL_IOC_HSM_STATE_SET: {
		struct hsm_state_set	*hss;
		int			 rc;

		hss = memdup_user((char __user *)arg, sizeof(*hss));
		if (IS_ERR(hss))
			return PTR_ERR(hss);

		rc = ll_hsm_state_set(inode, hss);

		kfree(hss);
		return rc;
	}
	case LL_IOC_HSM_ACTION: {
		struct md_op_data		*op_data;
		struct hsm_current_action	*hca;
		int				 rc;

		hca = kzalloc(sizeof(*hca), GFP_NOFS);
		if (!hca)
			return -ENOMEM;

		op_data = ll_prep_md_op_data(NULL, inode, NULL, NULL, 0, 0,
					     LUSTRE_OPC_ANY, hca);
		if (IS_ERR(op_data)) {
			kfree(hca);
			return PTR_ERR(op_data);
		}

		rc = obd_iocontrol(cmd, ll_i2mdexp(inode), sizeof(*op_data),
				   op_data, NULL);

		if (copy_to_user((char __user *)arg, hca, sizeof(*hca)))
			rc = -EFAULT;

		ll_finish_md_op_data(op_data);
		kfree(hca);
		return rc;
	}
	case LL_IOC_SET_LEASE: {
		struct ll_inode_info *lli = ll_i2info(inode);
		struct obd_client_handle *och = NULL;
		bool lease_broken;
		fmode_t fmode;

		switch (arg) {
		case LL_LEASE_WRLCK:
			if (!(file->f_mode & FMODE_WRITE))
				return -EPERM;
			fmode = FMODE_WRITE;
			break;
		case LL_LEASE_RDLCK:
			if (!(file->f_mode & FMODE_READ))
				return -EPERM;
			fmode = FMODE_READ;
			break;
		case LL_LEASE_UNLCK:
			mutex_lock(&lli->lli_och_mutex);
			if (fd->fd_lease_och) {
				och = fd->fd_lease_och;
				fd->fd_lease_och = NULL;
			}
			mutex_unlock(&lli->lli_och_mutex);

			if (!och)
				return -ENOLCK;

			fmode = och->och_flags;
			rc = ll_lease_close(och, inode, &lease_broken);
			if (rc < 0)
				return rc;

			if (lease_broken)
				fmode = 0;

			return ll_lease_type_from_fmode(fmode);
		default:
			return -EINVAL;
		}

		CDEBUG(D_INODE, "Set lease with mode %u\n", fmode);

		/* apply for lease */
		och = ll_lease_open(inode, file, fmode, 0);
		if (IS_ERR(och))
			return PTR_ERR(och);

		rc = 0;
		mutex_lock(&lli->lli_och_mutex);
		if (!fd->fd_lease_och) {
			fd->fd_lease_och = och;
			och = NULL;
		}
		mutex_unlock(&lli->lli_och_mutex);
		if (och) {
			/* impossible now that only excl is supported for now */
			ll_lease_close(och, inode, &lease_broken);
			rc = -EBUSY;
		}
		return rc;
	}
	case LL_IOC_GET_LEASE: {
		struct ll_inode_info *lli = ll_i2info(inode);
		struct ldlm_lock *lock = NULL;
		fmode_t fmode = 0;

		mutex_lock(&lli->lli_och_mutex);
		if (fd->fd_lease_och) {
			struct obd_client_handle *och = fd->fd_lease_och;

			lock = ldlm_handle2lock(&och->och_lease_handle);
			if (lock) {
				lock_res_and_lock(lock);
				if (!ldlm_is_cancel(lock))
					fmode = och->och_flags;
				unlock_res_and_lock(lock);
				LDLM_LOCK_PUT(lock);
			}
		}
		mutex_unlock(&lli->lli_och_mutex);
		return ll_lease_type_from_fmode(fmode);
	}
	case LL_IOC_HSM_IMPORT: {
		struct hsm_user_import *hui;

		hui = memdup_user((void __user *)arg, sizeof(*hui));
		if (IS_ERR(hui))
			return PTR_ERR(hui);

		rc = ll_hsm_import(inode, file, hui);

		kfree(hui);
		return rc;
	}
	default: {
		int err;

		if (ll_iocontrol_call(inode, file, cmd, arg, &err) ==
		     LLIOC_STOP)
			return err;

		return obd_iocontrol(cmd, ll_i2dtexp(inode), 0, NULL,
				     (void __user *)arg);
	}
	}
}

static loff_t ll_file_seek(struct file *file, loff_t offset, int origin)
{
	struct inode *inode = file_inode(file);
	loff_t retval, eof = 0;

	retval = offset + ((origin == SEEK_END) ? i_size_read(inode) :
			   (origin == SEEK_CUR) ? file->f_pos : 0);
	CDEBUG(D_VFSTRACE, "VFS Op:inode="DFID"(%p), to=%llu=%#llx(%d)\n",
	       PFID(ll_inode2fid(inode)), inode, retval, retval, origin);
	ll_stats_ops_tally(ll_i2sbi(inode), LPROC_LL_LLSEEK, 1);

	if (origin == SEEK_END || origin == SEEK_HOLE || origin == SEEK_DATA) {
		retval = ll_glimpse_size(inode);
		if (retval != 0)
			return retval;
		eof = i_size_read(inode);
	}

	return generic_file_llseek_size(file, offset, origin,
					ll_file_maxbytes(inode), eof);
}

static int ll_flush(struct file *file, fl_owner_t id)
{
	struct inode *inode = file_inode(file);
	struct ll_inode_info *lli = ll_i2info(inode);
	struct ll_file_data *fd = LUSTRE_FPRIVATE(file);
	int rc, err;

	LASSERT(!S_ISDIR(inode->i_mode));

	/* catch async errors that were recorded back when async writeback
	 * failed for pages in this mapping.
	 */
	rc = lli->lli_async_rc;
	lli->lli_async_rc = 0;
	if (lli->lli_clob) {
		err = lov_read_and_clear_async_rc(lli->lli_clob);
		if (!rc)
			rc = err;
	}

	/* The application has been told about write failure already.
	 * Do not report failure again.
	 */
	if (fd->fd_write_failed)
		return 0;
	return rc ? -EIO : 0;
}

/**
 * Called to make sure a portion of file has been written out.
 * if @mode is not CL_FSYNC_LOCAL, it will send OST_SYNC RPCs to OST.
 *
 * Return how many pages have been written.
 */
int cl_sync_file_range(struct inode *inode, loff_t start, loff_t end,
		       enum cl_fsync_mode mode, int ignore_layout)
{
	struct lu_env *env;
	struct cl_io *io;
	struct cl_fsync_io *fio;
	int result;
	int refcheck;

	if (mode != CL_FSYNC_NONE && mode != CL_FSYNC_LOCAL &&
	    mode != CL_FSYNC_DISCARD && mode != CL_FSYNC_ALL)
		return -EINVAL;

	env = cl_env_get(&refcheck);
	if (IS_ERR(env))
		return PTR_ERR(env);

	io = vvp_env_thread_io(env);
	io->ci_obj = ll_i2info(inode)->lli_clob;
	io->ci_ignore_layout = ignore_layout;

	/* initialize parameters for sync */
	fio = &io->u.ci_fsync;
	fio->fi_start = start;
	fio->fi_end = end;
	fio->fi_fid = ll_inode2fid(inode);
	fio->fi_mode = mode;
	fio->fi_nr_written = 0;

	if (cl_io_init(env, io, CIT_FSYNC, io->ci_obj) == 0)
		result = cl_io_loop(env, io);
	else
		result = io->ci_result;
	if (result == 0)
		result = fio->fi_nr_written;
	cl_io_fini(env, io);
	cl_env_put(env, &refcheck);

	return result;
}

int ll_fsync(struct file *file, loff_t start, loff_t end, int datasync)
{
	struct inode *inode = file_inode(file);
	struct ll_inode_info *lli = ll_i2info(inode);
	struct ptlrpc_request *req;
	int rc, err;

	CDEBUG(D_VFSTRACE, "VFS Op:inode="DFID"(%p)\n",
	       PFID(ll_inode2fid(inode)), inode);
	ll_stats_ops_tally(ll_i2sbi(inode), LPROC_LL_FSYNC, 1);

	rc = filemap_write_and_wait_range(inode->i_mapping, start, end);
	inode_lock(inode);

	/* catch async errors that were recorded back when async writeback
	 * failed for pages in this mapping.
	 */
	if (!S_ISDIR(inode->i_mode)) {
		err = lli->lli_async_rc;
		lli->lli_async_rc = 0;
		if (rc == 0)
			rc = err;
		if (lli->lli_clob) {
			err = lov_read_and_clear_async_rc(lli->lli_clob);
			if (rc == 0)
				rc = err;
		}
	}

	err = md_sync(ll_i2sbi(inode)->ll_md_exp, ll_inode2fid(inode), &req);
	if (!rc)
		rc = err;
	if (!err)
		ptlrpc_req_finished(req);

	if (S_ISREG(inode->i_mode)) {
		struct ll_file_data *fd = LUSTRE_FPRIVATE(file);

		err = cl_sync_file_range(inode, start, end, CL_FSYNC_ALL, 0);
		if (rc == 0 && err < 0)
			rc = err;
		if (rc < 0)
			fd->fd_write_failed = true;
		else
			fd->fd_write_failed = false;
	}

	inode_unlock(inode);
	return rc;
}

static int
ll_file_flock(struct file *file, int cmd, struct file_lock *file_lock)
{
	struct inode *inode = file_inode(file);
	struct ll_sb_info *sbi = ll_i2sbi(inode);
	struct ldlm_enqueue_info einfo = {
		.ei_type	= LDLM_FLOCK,
		.ei_cb_cp	= ldlm_flock_completion_ast,
		.ei_cbdata	= file_lock,
	};
	struct md_op_data *op_data;
	struct lustre_handle lockh = {0};
	ldlm_policy_data_t flock = { {0} };
	int fl_type = file_lock->fl_type;
	__u64 flags = 0;
	int rc;
	int rc2 = 0;

	CDEBUG(D_VFSTRACE, "VFS Op:inode="DFID" file_lock=%p\n",
	       PFID(ll_inode2fid(inode)), file_lock);

	ll_stats_ops_tally(ll_i2sbi(inode), LPROC_LL_FLOCK, 1);

	if (file_lock->fl_flags & FL_FLOCK)
		LASSERT((cmd == F_SETLKW) || (cmd == F_SETLK));
	else if (!(file_lock->fl_flags & FL_POSIX))
		return -EINVAL;

	flock.l_flock.owner = (unsigned long)file_lock->fl_owner;
	flock.l_flock.pid = file_lock->fl_pid;
	flock.l_flock.start = file_lock->fl_start;
	flock.l_flock.end = file_lock->fl_end;

	/* Somewhat ugly workaround for svc lockd.
	 * lockd installs custom fl_lmops->lm_compare_owner that checks
	 * for the fl_owner to be the same (which it always is on local node
	 * I guess between lockd processes) and then compares pid.
	 * As such we assign pid to the owner field to make it all work,
	 * conflict with normal locks is unlikely since pid space and
	 * pointer space for current->files are not intersecting
	 */
	if (file_lock->fl_lmops && file_lock->fl_lmops->lm_compare_owner)
		flock.l_flock.owner = (unsigned long)file_lock->fl_pid;

	switch (fl_type) {
	case F_RDLCK:
		einfo.ei_mode = LCK_PR;
		break;
	case F_UNLCK:
		/* An unlock request may or may not have any relation to
		 * existing locks so we may not be able to pass a lock handle
		 * via a normal ldlm_lock_cancel() request. The request may even
		 * unlock a byte range in the middle of an existing lock. In
		 * order to process an unlock request we need all of the same
		 * information that is given with a normal read or write record
		 * lock request. To avoid creating another ldlm unlock (cancel)
		 * message we'll treat a LCK_NL flock request as an unlock.
		 */
		einfo.ei_mode = LCK_NL;
		break;
	case F_WRLCK:
		einfo.ei_mode = LCK_PW;
		break;
	default:
		CDEBUG(D_INFO, "Unknown fcntl lock type: %d\n", fl_type);
		return -ENOTSUPP;
	}

	switch (cmd) {
	case F_SETLKW:
#ifdef F_SETLKW64
	case F_SETLKW64:
#endif
		flags = 0;
		break;
	case F_SETLK:
#ifdef F_SETLK64
	case F_SETLK64:
#endif
		flags = LDLM_FL_BLOCK_NOWAIT;
		break;
	case F_GETLK:
#ifdef F_GETLK64
	case F_GETLK64:
#endif
		flags = LDLM_FL_TEST_LOCK;
		break;
	default:
		CERROR("unknown fcntl lock command: %d\n", cmd);
		return -EINVAL;
	}

	/*
	 * Save the old mode so that if the mode in the lock changes we
	 * can decrement the appropriate reader or writer refcount.
	 */
	file_lock->fl_type = einfo.ei_mode;

	op_data = ll_prep_md_op_data(NULL, inode, NULL, NULL, 0, 0,
				     LUSTRE_OPC_ANY, NULL);
	if (IS_ERR(op_data))
		return PTR_ERR(op_data);

	CDEBUG(D_DLMTRACE, "inode="DFID", pid=%u, flags=%#llx, mode=%u, start=%llu, end=%llu\n",
	       PFID(ll_inode2fid(inode)), flock.l_flock.pid, flags,
	       einfo.ei_mode, flock.l_flock.start, flock.l_flock.end);

	rc = md_enqueue(sbi->ll_md_exp, &einfo, &flock, NULL, op_data, &lockh,
			flags);

	/* Restore the file lock type if not TEST lock. */
	if (!(flags & LDLM_FL_TEST_LOCK))
		file_lock->fl_type = fl_type;

	if ((rc == 0 || file_lock->fl_type == F_UNLCK) &&
	    !(flags & LDLM_FL_TEST_LOCK))
		rc2  = locks_lock_file_wait(file, file_lock);

	if (rc2 && file_lock->fl_type != F_UNLCK) {
		einfo.ei_mode = LCK_NL;
		md_enqueue(sbi->ll_md_exp, &einfo, &flock, NULL, op_data,
			   &lockh, flags);
		rc = rc2;
	}

	ll_finish_md_op_data(op_data);

	return rc;
}

int ll_get_fid_by_name(struct inode *parent, const char *name,
		       int namelen, struct lu_fid *fid)
{
	struct md_op_data *op_data = NULL;
	struct ptlrpc_request *req;
	struct mdt_body *body;
	int rc;

	op_data = ll_prep_md_op_data(NULL, parent, NULL, name, namelen, 0,
				     LUSTRE_OPC_ANY, NULL);
	if (IS_ERR(op_data))
		return PTR_ERR(op_data);

	op_data->op_valid = OBD_MD_FLID;
	rc = md_getattr_name(ll_i2sbi(parent)->ll_md_exp, op_data, &req);
	ll_finish_md_op_data(op_data);
	if (rc < 0)
		return rc;

	body = req_capsule_server_get(&req->rq_pill, &RMF_MDT_BODY);
	if (!body) {
		rc = -EFAULT;
		goto out_req;
	}
	if (fid)
		*fid = body->mbo_fid1;
out_req:
	ptlrpc_req_finished(req);
	return rc;
}

int ll_migrate(struct inode *parent, struct file *file, int mdtidx,
	       const char *name, int namelen)
{
	struct ptlrpc_request *request = NULL;
	struct inode *child_inode = NULL;
	struct dentry *dchild = NULL;
	struct md_op_data *op_data;
	struct qstr qstr;
	int rc;

	CDEBUG(D_VFSTRACE, "migrate %s under "DFID" to MDT%d\n",
	       name, PFID(ll_inode2fid(parent)), mdtidx);

	op_data = ll_prep_md_op_data(NULL, parent, NULL, name, namelen,
				     0, LUSTRE_OPC_ANY, NULL);
	if (IS_ERR(op_data))
		return PTR_ERR(op_data);

	/* Get child FID first */
	qstr.hash = full_name_hash(parent, name, namelen);
	qstr.name = name;
	qstr.len = namelen;
	dchild = d_lookup(file_dentry(file), &qstr);
	if (dchild) {
		op_data->op_fid3 = *ll_inode2fid(dchild->d_inode);
		if (dchild->d_inode) {
			child_inode = igrab(dchild->d_inode);
			if (child_inode) {
				inode_lock(child_inode);
				op_data->op_fid3 = *ll_inode2fid(child_inode);
				ll_invalidate_aliases(child_inode);
			}
		}
		dput(dchild);
	} else {
		rc = ll_get_fid_by_name(parent, name, namelen,
					&op_data->op_fid3);
		if (rc)
			goto out_free;
	}

	if (!fid_is_sane(&op_data->op_fid3)) {
		CERROR("%s: migrate %s, but fid "DFID" is insane\n",
		       ll_get_fsname(parent->i_sb, NULL, 0), name,
		       PFID(&op_data->op_fid3));
		rc = -EINVAL;
		goto out_free;
	}

	rc = ll_get_mdt_idx_by_fid(ll_i2sbi(parent), &op_data->op_fid3);
	if (rc < 0)
		goto out_free;

	if (rc == mdtidx) {
		CDEBUG(D_INFO, "%s:"DFID" is already on MDT%d.\n", name,
		       PFID(&op_data->op_fid3), mdtidx);
		rc = 0;
		goto out_free;
	}

	op_data->op_mds = mdtidx;
	op_data->op_cli_flags = CLI_MIGRATE;
	rc = md_rename(ll_i2sbi(parent)->ll_md_exp, op_data, name,
		       namelen, name, namelen, &request);
	if (!rc)
		ll_update_times(request, parent);

	ptlrpc_req_finished(request);

out_free:
	if (child_inode) {
		clear_nlink(child_inode);
		inode_unlock(child_inode);
		iput(child_inode);
	}

	ll_finish_md_op_data(op_data);
	return rc;
}

static int
ll_file_noflock(struct file *file, int cmd, struct file_lock *file_lock)
{
	return -ENOSYS;
}

/**
 * test if some locks matching bits and l_req_mode are acquired
 * - bits can be in different locks
 * - if found clear the common lock bits in *bits
 * - the bits not found, are kept in *bits
 * \param inode [IN]
 * \param bits [IN] searched lock bits [IN]
 * \param l_req_mode [IN] searched lock mode
 * \retval boolean, true iff all bits are found
 */
int ll_have_md_lock(struct inode *inode, __u64 *bits,
		    enum ldlm_mode l_req_mode)
{
	struct lustre_handle lockh;
	ldlm_policy_data_t policy;
	enum ldlm_mode mode = (l_req_mode == LCK_MINMODE) ?
			      (LCK_CR | LCK_CW | LCK_PR | LCK_PW) : l_req_mode;
	struct lu_fid *fid;
	__u64 flags;
	int i;

	if (!inode)
		return 0;

	fid = &ll_i2info(inode)->lli_fid;
	CDEBUG(D_INFO, "trying to match res "DFID" mode %s\n", PFID(fid),
	       ldlm_lockname[mode]);

	flags = LDLM_FL_BLOCK_GRANTED | LDLM_FL_CBPENDING | LDLM_FL_TEST_LOCK;
	for (i = 0; i <= MDS_INODELOCK_MAXSHIFT && *bits != 0; i++) {
		policy.l_inodebits.bits = *bits & (1 << i);
		if (policy.l_inodebits.bits == 0)
			continue;

		if (md_lock_match(ll_i2mdexp(inode), flags, fid, LDLM_IBITS,
				  &policy, mode, &lockh)) {
			struct ldlm_lock *lock;

			lock = ldlm_handle2lock(&lockh);
			if (lock) {
				*bits &=
				      ~(lock->l_policy_data.l_inodebits.bits);
				LDLM_LOCK_PUT(lock);
			} else {
				*bits &= ~policy.l_inodebits.bits;
			}
		}
	}
	return *bits == 0;
}

enum ldlm_mode ll_take_md_lock(struct inode *inode, __u64 bits,
			       struct lustre_handle *lockh, __u64 flags,
			       enum ldlm_mode mode)
{
	ldlm_policy_data_t policy = { .l_inodebits = {bits} };
	struct lu_fid *fid;

	fid = &ll_i2info(inode)->lli_fid;
	CDEBUG(D_INFO, "trying to match res "DFID"\n", PFID(fid));

	return md_lock_match(ll_i2mdexp(inode), flags | LDLM_FL_BLOCK_GRANTED,
			     fid, LDLM_IBITS, &policy, mode, lockh);
}

static int ll_inode_revalidate_fini(struct inode *inode, int rc)
{
	/* Already unlinked. Just update nlink and return success */
	if (rc == -ENOENT) {
		clear_nlink(inode);
		/* If it is striped directory, and there is bad stripe
		 * Let's revalidate the dentry again, instead of returning
		 * error
		 */
		if (S_ISDIR(inode->i_mode) && ll_i2info(inode)->lli_lsm_md)
			return 0;

		/* This path cannot be hit for regular files unless in
		 * case of obscure races, so no need to validate size.
		 */
		if (!S_ISREG(inode->i_mode) && !S_ISDIR(inode->i_mode))
			return 0;
	} else if (rc != 0) {
		CDEBUG_LIMIT((rc == -EACCES || rc == -EIDRM) ? D_INFO : D_ERROR,
			     "%s: revalidate FID "DFID" error: rc = %d\n",
			     ll_get_fsname(inode->i_sb, NULL, 0),
			     PFID(ll_inode2fid(inode)), rc);
	}

	return rc;
}

static int __ll_inode_revalidate(struct dentry *dentry, __u64 ibits)
{
	struct inode *inode = d_inode(dentry);
	struct ptlrpc_request *req = NULL;
	struct obd_export *exp;
	int rc = 0;

	CDEBUG(D_VFSTRACE, "VFS Op:inode="DFID"(%p),name=%pd\n",
	       PFID(ll_inode2fid(inode)), inode, dentry);

	exp = ll_i2mdexp(inode);

	/* XXX: Enable OBD_CONNECT_ATTRFID to reduce unnecessary getattr RPC.
	 *      But under CMD case, it caused some lock issues, should be fixed
	 *      with new CMD ibits lock. See bug 12718
	 */
	if (exp_connect_flags(exp) & OBD_CONNECT_ATTRFID) {
		struct lookup_intent oit = { .it_op = IT_GETATTR };
		struct md_op_data *op_data;

		if (ibits == MDS_INODELOCK_LOOKUP)
			oit.it_op = IT_LOOKUP;

		/* Call getattr by fid, so do not provide name at all. */
		op_data = ll_prep_md_op_data(NULL, inode,
					     inode, NULL, 0, 0,
					     LUSTRE_OPC_ANY, NULL);
		if (IS_ERR(op_data))
			return PTR_ERR(op_data);

		rc = md_intent_lock(exp, op_data, &oit, &req,
				    &ll_md_blocking_ast, 0);
		ll_finish_md_op_data(op_data);
		if (rc < 0) {
			rc = ll_inode_revalidate_fini(inode, rc);
			goto out;
		}

		rc = ll_revalidate_it_finish(req, &oit, inode);
		if (rc != 0) {
			ll_intent_release(&oit);
			goto out;
		}

		/* Unlinked? Unhash dentry, so it is not picked up later by
		 * do_lookup() -> ll_revalidate_it(). We cannot use d_drop
		 * here to preserve get_cwd functionality on 2.6.
		 * Bug 10503
		 */
		if (!d_inode(dentry)->i_nlink) {
			spin_lock(&inode->i_lock);
			d_lustre_invalidate(dentry, 0);
			spin_unlock(&inode->i_lock);
		}

		ll_lookup_finish_locks(&oit, inode);
	} else if (!ll_have_md_lock(d_inode(dentry), &ibits, LCK_MINMODE)) {
		struct ll_sb_info *sbi = ll_i2sbi(d_inode(dentry));
		u64 valid = OBD_MD_FLGETATTR;
		struct md_op_data *op_data;
		int ealen = 0;

		if (S_ISREG(inode->i_mode)) {
			rc = ll_get_default_mdsize(sbi, &ealen);
			if (rc)
				return rc;
			valid |= OBD_MD_FLEASIZE | OBD_MD_FLMODEASIZE;
		}

		op_data = ll_prep_md_op_data(NULL, inode, NULL, NULL,
					     0, ealen, LUSTRE_OPC_ANY,
					     NULL);
		if (IS_ERR(op_data))
			return PTR_ERR(op_data);

		op_data->op_valid = valid;
		rc = md_getattr(sbi->ll_md_exp, op_data, &req);
		ll_finish_md_op_data(op_data);
		if (rc)
			return ll_inode_revalidate_fini(inode, rc);

		rc = ll_prep_inode(&inode, req, NULL, NULL);
	}
out:
	ptlrpc_req_finished(req);
	return rc;
}

static int ll_merge_md_attr(struct inode *inode)
{
	struct cl_attr attr = { 0 };
	int rc;

	LASSERT(ll_i2info(inode)->lli_lsm_md);
	rc = md_merge_attr(ll_i2mdexp(inode), ll_i2info(inode)->lli_lsm_md,
			   &attr, ll_md_blocking_ast);
	if (rc)
		return rc;

	set_nlink(inode, attr.cat_nlink);
	inode->i_blocks = attr.cat_blocks;
	i_size_write(inode, attr.cat_size);

	ll_i2info(inode)->lli_atime = attr.cat_atime;
	ll_i2info(inode)->lli_mtime = attr.cat_mtime;
	ll_i2info(inode)->lli_ctime = attr.cat_ctime;

	return 0;
}

static int ll_inode_revalidate(struct dentry *dentry, __u64 ibits)
{
	struct inode *inode = d_inode(dentry);
	int rc;

	rc = __ll_inode_revalidate(dentry, ibits);
	if (rc != 0)
		return rc;

	/* if object isn't regular file, don't validate size */
	if (!S_ISREG(inode->i_mode)) {
		if (S_ISDIR(inode->i_mode) &&
		    ll_i2info(inode)->lli_lsm_md) {
			rc = ll_merge_md_attr(inode);
			if (rc)
				return rc;
		}

		LTIME_S(inode->i_atime) = ll_i2info(inode)->lli_atime;
		LTIME_S(inode->i_mtime) = ll_i2info(inode)->lli_mtime;
		LTIME_S(inode->i_ctime) = ll_i2info(inode)->lli_ctime;
	} else {
		/* In case of restore, the MDT has the right size and has
		 * already send it back without granting the layout lock,
		 * inode is up-to-date so glimpse is useless.
		 * Also to glimpse we need the layout, in case of a running
		 * restore the MDT holds the layout lock so the glimpse will
		 * block up to the end of restore (getattr will block)
		 */
		if (!(ll_i2info(inode)->lli_flags & LLIF_FILE_RESTORING))
			rc = ll_glimpse_size(inode);
	}
	return rc;
}

int ll_getattr(struct vfsmount *mnt, struct dentry *de, struct kstat *stat)
{
	struct inode *inode = d_inode(de);
	struct ll_sb_info *sbi = ll_i2sbi(inode);
	struct ll_inode_info *lli = ll_i2info(inode);
	int res;

	res = ll_inode_revalidate(de, MDS_INODELOCK_UPDATE |
				      MDS_INODELOCK_LOOKUP);
	ll_stats_ops_tally(sbi, LPROC_LL_GETATTR, 1);

	if (res)
		return res;

	OBD_FAIL_TIMEOUT(OBD_FAIL_GETATTR_DELAY, 30);

	stat->dev = inode->i_sb->s_dev;
	if (ll_need_32bit_api(sbi))
		stat->ino = cl_fid_build_ino(&lli->lli_fid, 1);
	else
		stat->ino = inode->i_ino;
	stat->mode = inode->i_mode;
	stat->uid = inode->i_uid;
	stat->gid = inode->i_gid;
	stat->rdev = inode->i_rdev;
	stat->atime = inode->i_atime;
	stat->mtime = inode->i_mtime;
	stat->ctime = inode->i_ctime;
	stat->blksize = 1 << inode->i_blkbits;

	stat->nlink = inode->i_nlink;
	stat->size = i_size_read(inode);
	stat->blocks = inode->i_blocks;

	return 0;
}

static int ll_fiemap(struct inode *inode, struct fiemap_extent_info *fieinfo,
		     __u64 start, __u64 len)
{
	int rc;
	size_t num_bytes;
	struct fiemap *fiemap;
	unsigned int extent_count = fieinfo->fi_extents_max;

	num_bytes = sizeof(*fiemap) + (extent_count *
				       sizeof(struct fiemap_extent));
	fiemap = libcfs_kvzalloc(num_bytes, GFP_NOFS);
	if (!fiemap)
		return -ENOMEM;

	fiemap->fm_flags = fieinfo->fi_flags;
	fiemap->fm_extent_count = fieinfo->fi_extents_max;
	fiemap->fm_start = start;
	fiemap->fm_length = len;

	if (extent_count > 0 &&
	    copy_from_user(&fiemap->fm_extents[0], fieinfo->fi_extents_start,
			   sizeof(struct fiemap_extent))) {
		rc = -EFAULT;
		goto out;
	}

	rc = ll_do_fiemap(inode, fiemap, num_bytes);

	fieinfo->fi_flags = fiemap->fm_flags;
	fieinfo->fi_extents_mapped = fiemap->fm_mapped_extents;
	if (extent_count > 0 &&
	    copy_to_user(fieinfo->fi_extents_start, &fiemap->fm_extents[0],
			 fiemap->fm_mapped_extents *
			 sizeof(struct fiemap_extent))) {
		rc = -EFAULT;
		goto out;
	}
out:
	kvfree(fiemap);
	return rc;
}

struct posix_acl *ll_get_acl(struct inode *inode, int type)
{
	struct ll_inode_info *lli = ll_i2info(inode);
	struct posix_acl *acl = NULL;

	spin_lock(&lli->lli_lock);
	/* VFS' acl_permission_check->check_acl will release the refcount */
	acl = posix_acl_dup(lli->lli_posix_acl);
#ifdef CONFIG_FS_POSIX_ACL
	forget_cached_acl(inode, type);
#endif
	spin_unlock(&lli->lli_lock);

	return acl;
}

int ll_inode_permission(struct inode *inode, int mask)
{
	struct ll_sb_info *sbi;
	struct root_squash_info *squash;
	const struct cred *old_cred = NULL;
	struct cred *cred = NULL;
	bool squash_id = false;
	cfs_cap_t cap;
	int rc = 0;

	if (mask & MAY_NOT_BLOCK)
		return -ECHILD;

       /* as root inode are NOT getting validated in lookup operation,
	* need to do it before permission check.
	*/

	if (is_root_inode(inode)) {
		rc = __ll_inode_revalidate(inode->i_sb->s_root,
					   MDS_INODELOCK_LOOKUP);
		if (rc)
			return rc;
	}

	CDEBUG(D_VFSTRACE, "VFS Op:inode="DFID"(%p), inode mode %x mask %o\n",
	       PFID(ll_inode2fid(inode)), inode, inode->i_mode, mask);

	/* squash fsuid/fsgid if needed */
	sbi = ll_i2sbi(inode);
	squash = &sbi->ll_squash;
	if (unlikely(squash->rsi_uid &&
		     uid_eq(current_fsuid(), GLOBAL_ROOT_UID) &&
		     !(sbi->ll_flags & LL_SBI_NOROOTSQUASH))) {
		squash_id = true;
	}

	if (squash_id) {
		CDEBUG(D_OTHER, "squash creds (%d:%d)=>(%d:%d)\n",
		       __kuid_val(current_fsuid()), __kgid_val(current_fsgid()),
		       squash->rsi_uid, squash->rsi_gid);

		/*
		 * update current process's credentials
		 * and FS capability
		 */
		cred = prepare_creds();
		if (!cred)
			return -ENOMEM;

		cred->fsuid = make_kuid(&init_user_ns, squash->rsi_uid);
		cred->fsgid = make_kgid(&init_user_ns, squash->rsi_gid);
		for (cap = 0; cap < sizeof(cfs_cap_t) * 8; cap++) {
			if ((1 << cap) & CFS_CAP_FS_MASK)
				cap_lower(cred->cap_effective, cap);
		}
		old_cred = override_creds(cred);
	}

	ll_stats_ops_tally(ll_i2sbi(inode), LPROC_LL_INODE_PERM, 1);
	rc = generic_permission(inode, mask);

	/* restore current process's credentials and FS capability */
	if (squash_id) {
		revert_creds(old_cred);
		put_cred(cred);
	}

	return rc;
}

/* -o localflock - only provides locally consistent flock locks */
struct file_operations ll_file_operations = {
	.read_iter = ll_file_read_iter,
	.write_iter = ll_file_write_iter,
	.unlocked_ioctl = ll_file_ioctl,
	.open	   = ll_file_open,
	.release	= ll_file_release,
	.mmap	   = ll_file_mmap,
	.llseek	 = ll_file_seek,
	.splice_read    = generic_file_splice_read,
	.fsync	  = ll_fsync,
	.flush	  = ll_flush
};

struct file_operations ll_file_operations_flock = {
	.read_iter    = ll_file_read_iter,
	.write_iter   = ll_file_write_iter,
	.unlocked_ioctl = ll_file_ioctl,
	.open	   = ll_file_open,
	.release	= ll_file_release,
	.mmap	   = ll_file_mmap,
	.llseek	 = ll_file_seek,
	.splice_read    = generic_file_splice_read,
	.fsync	  = ll_fsync,
	.flush	  = ll_flush,
	.flock	  = ll_file_flock,
	.lock	   = ll_file_flock
};

/* These are for -o noflock - to return ENOSYS on flock calls */
struct file_operations ll_file_operations_noflock = {
	.read_iter    = ll_file_read_iter,
	.write_iter   = ll_file_write_iter,
	.unlocked_ioctl = ll_file_ioctl,
	.open	   = ll_file_open,
	.release	= ll_file_release,
	.mmap	   = ll_file_mmap,
	.llseek	 = ll_file_seek,
	.splice_read    = generic_file_splice_read,
	.fsync	  = ll_fsync,
	.flush	  = ll_flush,
	.flock	  = ll_file_noflock,
	.lock	   = ll_file_noflock
};

const struct inode_operations ll_file_inode_operations = {
	.setattr	= ll_setattr,
	.getattr	= ll_getattr,
	.permission	= ll_inode_permission,
	.listxattr	= ll_listxattr,
	.fiemap		= ll_fiemap,
	.get_acl	= ll_get_acl,
};

/* dynamic ioctl number support routines */
static struct llioc_ctl_data {
	struct rw_semaphore	ioc_sem;
	struct list_head	      ioc_head;
} llioc = {
	__RWSEM_INITIALIZER(llioc.ioc_sem),
	LIST_HEAD_INIT(llioc.ioc_head)
};

struct llioc_data {
	struct list_head	      iocd_list;
	unsigned int	    iocd_size;
	llioc_callback_t	iocd_cb;
	unsigned int	    iocd_count;
	unsigned int	    iocd_cmd[0];
};

void *ll_iocontrol_register(llioc_callback_t cb, int count, unsigned int *cmd)
{
	unsigned int size;
	struct llioc_data *in_data = NULL;

	if (!cb || !cmd || count > LLIOC_MAX_CMD || count < 0)
		return NULL;

	size = sizeof(*in_data) + count * sizeof(unsigned int);
	in_data = kzalloc(size, GFP_NOFS);
	if (!in_data)
		return NULL;

	in_data->iocd_size = size;
	in_data->iocd_cb = cb;
	in_data->iocd_count = count;
	memcpy(in_data->iocd_cmd, cmd, sizeof(unsigned int) * count);

	down_write(&llioc.ioc_sem);
	list_add_tail(&in_data->iocd_list, &llioc.ioc_head);
	up_write(&llioc.ioc_sem);

	return in_data;
}
EXPORT_SYMBOL(ll_iocontrol_register);

void ll_iocontrol_unregister(void *magic)
{
	struct llioc_data *tmp;

	if (!magic)
		return;

	down_write(&llioc.ioc_sem);
	list_for_each_entry(tmp, &llioc.ioc_head, iocd_list) {
		if (tmp == magic) {
			list_del(&tmp->iocd_list);
			up_write(&llioc.ioc_sem);

			kfree(tmp);
			return;
		}
	}
	up_write(&llioc.ioc_sem);

	CWARN("didn't find iocontrol register block with magic: %p\n", magic);
}
EXPORT_SYMBOL(ll_iocontrol_unregister);

static enum llioc_iter
ll_iocontrol_call(struct inode *inode, struct file *file,
		  unsigned int cmd, unsigned long arg, int *rcp)
{
	enum llioc_iter ret = LLIOC_CONT;
	struct llioc_data *data;
	int rc = -EINVAL, i;

	down_read(&llioc.ioc_sem);
	list_for_each_entry(data, &llioc.ioc_head, iocd_list) {
		for (i = 0; i < data->iocd_count; i++) {
			if (cmd != data->iocd_cmd[i])
				continue;

			ret = data->iocd_cb(inode, file, cmd, arg, data, &rc);
			break;
		}

		if (ret == LLIOC_STOP)
			break;
	}
	up_read(&llioc.ioc_sem);

	if (rcp)
		*rcp = rc;
	return ret;
}

int ll_layout_conf(struct inode *inode, const struct cl_object_conf *conf)
{
	struct ll_inode_info *lli = ll_i2info(inode);
	struct cl_object *obj = lli->lli_clob;
	struct lu_env *env;
	int rc;
	int refcheck;

	if (!obj)
		return 0;

	env = cl_env_get(&refcheck);
	if (IS_ERR(env))
		return PTR_ERR(env);

	rc = cl_conf_set(env, obj, conf);
	if (rc < 0)
		goto out;

	if (conf->coc_opc == OBJECT_CONF_SET) {
		struct ldlm_lock *lock = conf->coc_lock;
		struct cl_layout cl = {
			.cl_layout_gen = 0,
		};

		LASSERT(lock);
		LASSERT(ldlm_has_layout(lock));

		/* it can only be allowed to match after layout is
		 * applied to inode otherwise false layout would be
		 * seen. Applying layout should happen before dropping
		 * the intent lock.
		 */
		ldlm_lock_allow_match(lock);

		rc = cl_object_layout_get(env, obj, &cl);
		if (rc < 0)
			goto out;

		CDEBUG(D_VFSTRACE, DFID ": layout version change: %u -> %u\n",
		       PFID(&lli->lli_fid), ll_layout_version_get(lli),
		       cl.cl_layout_gen);
		ll_layout_version_set(lli, cl.cl_layout_gen);
	}
out:
	cl_env_put(env, &refcheck);
	return rc;
}

/* Fetch layout from MDT with getxattr request, if it's not ready yet */
static int ll_layout_fetch(struct inode *inode, struct ldlm_lock *lock)

{
	struct ll_sb_info *sbi = ll_i2sbi(inode);
	struct ptlrpc_request *req;
	struct mdt_body *body;
	void *lvbdata;
	void *lmm;
	int lmmsize;
	int rc;

	CDEBUG(D_INODE, DFID" LVB_READY=%d l_lvb_data=%p l_lvb_len=%d\n",
	       PFID(ll_inode2fid(inode)), ldlm_is_lvb_ready(lock),
	       lock->l_lvb_data, lock->l_lvb_len);

	if (lock->l_lvb_data && ldlm_is_lvb_ready(lock))
		return 0;

	/* if layout lock was granted right away, the layout is returned
	 * within DLM_LVB of dlm reply; otherwise if the lock was ever
	 * blocked and then granted via completion ast, we have to fetch
	 * layout here. Please note that we can't use the LVB buffer in
	 * completion AST because it doesn't have a large enough buffer
	 */
	rc = ll_get_default_mdsize(sbi, &lmmsize);
	if (rc == 0)
		rc = md_getxattr(sbi->ll_md_exp, ll_inode2fid(inode),
				 OBD_MD_FLXATTR, XATTR_NAME_LOV, NULL, 0,
				 lmmsize, 0, &req);
	if (rc < 0)
		return rc;

	body = req_capsule_server_get(&req->rq_pill, &RMF_MDT_BODY);
	if (!body) {
		rc = -EPROTO;
		goto out;
	}

	lmmsize = body->mbo_eadatasize;
	if (lmmsize == 0) /* empty layout */ {
		rc = 0;
		goto out;
	}

	lmm = req_capsule_server_sized_get(&req->rq_pill, &RMF_EADATA, lmmsize);
	if (!lmm) {
		rc = -EFAULT;
		goto out;
	}

	lvbdata = libcfs_kvzalloc(lmmsize, GFP_NOFS);
	if (!lvbdata) {
		rc = -ENOMEM;
		goto out;
	}

	memcpy(lvbdata, lmm, lmmsize);
	lock_res_and_lock(lock);
	if (lock->l_lvb_data)
		kvfree(lock->l_lvb_data);

	lock->l_lvb_data = lvbdata;
	lock->l_lvb_len = lmmsize;
	unlock_res_and_lock(lock);

out:
	ptlrpc_req_finished(req);
	return rc;
}

/**
 * Apply the layout to the inode. Layout lock is held and will be released
 * in this function.
 */
static int ll_layout_lock_set(struct lustre_handle *lockh, enum ldlm_mode mode,
			      struct inode *inode)
{
	struct ll_inode_info *lli = ll_i2info(inode);
	struct ll_sb_info    *sbi = ll_i2sbi(inode);
	struct ldlm_lock *lock;
	struct cl_object_conf conf;
	int rc = 0;
	bool lvb_ready;
	bool wait_layout = false;

	LASSERT(lustre_handle_is_used(lockh));

	lock = ldlm_handle2lock(lockh);
	LASSERT(lock);
	LASSERT(ldlm_has_layout(lock));

	LDLM_DEBUG(lock, "File " DFID "(%p) being reconfigured",
		   PFID(&lli->lli_fid), inode);

	/* in case this is a caching lock and reinstate with new inode */
	md_set_lock_data(sbi->ll_md_exp, lockh, inode, NULL);

	lock_res_and_lock(lock);
	lvb_ready = ldlm_is_lvb_ready(lock);
	unlock_res_and_lock(lock);
	/* checking lvb_ready is racy but this is okay. The worst case is
	 * that multi processes may configure the file on the same time.
	 */
	if (lvb_ready) {
		rc = 0;
		goto out;
	}

	rc = ll_layout_fetch(inode, lock);
	if (rc < 0)
		goto out;

	/* for layout lock, lmm is returned in lock's lvb.
	 * lvb_data is immutable if the lock is held so it's safe to access it
	 * without res lock.
	 *
	 * set layout to file. Unlikely this will fail as old layout was
	 * surely eliminated
	 */
	memset(&conf, 0, sizeof(conf));
	conf.coc_opc = OBJECT_CONF_SET;
	conf.coc_inode = inode;
	conf.coc_lock = lock;
	conf.u.coc_layout.lb_buf = lock->l_lvb_data;
	conf.u.coc_layout.lb_len = lock->l_lvb_len;
	rc = ll_layout_conf(inode, &conf);

	/* refresh layout failed, need to wait */
	wait_layout = rc == -EBUSY;

out:
	LDLM_LOCK_PUT(lock);
	ldlm_lock_decref(lockh, mode);

	/* wait for IO to complete if it's still being used. */
	if (wait_layout) {
		CDEBUG(D_INODE, "%s: "DFID"(%p) wait for layout reconf\n",
		       ll_get_fsname(inode->i_sb, NULL, 0),
		       PFID(&lli->lli_fid), inode);

		memset(&conf, 0, sizeof(conf));
		conf.coc_opc = OBJECT_CONF_WAIT;
		conf.coc_inode = inode;
		rc = ll_layout_conf(inode, &conf);
		if (rc == 0)
			rc = -EAGAIN;

		CDEBUG(D_INODE, "%s: file="DFID" waiting layout return: %d.\n",
		       ll_get_fsname(inode->i_sb, NULL, 0),
		       PFID(&lli->lli_fid), rc);
	}
	return rc;
}

static int ll_layout_refresh_locked(struct inode *inode)
{
	struct ll_inode_info  *lli = ll_i2info(inode);
	struct ll_sb_info     *sbi = ll_i2sbi(inode);
	struct md_op_data     *op_data;
	struct lookup_intent   it;
	struct lustre_handle   lockh;
	enum ldlm_mode	       mode;
	struct ldlm_enqueue_info einfo = {
		.ei_type = LDLM_IBITS,
		.ei_mode = LCK_CR,
		.ei_cb_bl = &ll_md_blocking_ast,
		.ei_cb_cp = &ldlm_completion_ast,
	};
	int rc;

again:
	/* mostly layout lock is caching on the local side, so try to match
	 * it before grabbing layout lock mutex.
	 */
	mode = ll_take_md_lock(inode, MDS_INODELOCK_LAYOUT, &lockh, 0,
			       LCK_CR | LCK_CW | LCK_PR | LCK_PW);
	if (mode != 0) { /* hit cached lock */
		rc = ll_layout_lock_set(&lockh, mode, inode);
		if (rc == -EAGAIN)
			goto again;
		return rc;
	}

	op_data = ll_prep_md_op_data(NULL, inode, inode, NULL,
				     0, 0, LUSTRE_OPC_ANY, NULL);
	if (IS_ERR(op_data))
		return PTR_ERR(op_data);

	/* have to enqueue one */
	memset(&it, 0, sizeof(it));
	it.it_op = IT_LAYOUT;
	lockh.cookie = 0ULL;

	LDLM_DEBUG_NOLOCK("%s: requeue layout lock for file "DFID"(%p)",
			  ll_get_fsname(inode->i_sb, NULL, 0),
			  PFID(&lli->lli_fid), inode);

	rc = md_enqueue(sbi->ll_md_exp, &einfo, NULL, &it, op_data, &lockh, 0);
	ptlrpc_req_finished(it.it_request);
	it.it_request = NULL;

	ll_finish_md_op_data(op_data);

	mode = it.it_lock_mode;
	it.it_lock_mode = 0;
	ll_intent_drop_lock(&it);

	if (rc == 0) {
		/* set lock data in case this is a new lock */
		ll_set_lock_data(sbi->ll_md_exp, inode, &it, NULL);
		rc = ll_layout_lock_set(&lockh, mode, inode);
		if (rc == -EAGAIN)
			goto again;
	}

	return rc;
}

/**
 * This function checks if there exists a LAYOUT lock on the client side,
 * or enqueues it if it doesn't have one in cache.
 *
 * This function will not hold layout lock so it may be revoked any time after
 * this function returns. Any operations depend on layout should be redone
 * in that case.
 *
 * This function should be called before lov_io_init() to get an uptodate
 * layout version, the caller should save the version number and after IO
 * is finished, this function should be called again to verify that layout
 * is not changed during IO time.
 */
int ll_layout_refresh(struct inode *inode, __u32 *gen)
{
	struct ll_inode_info *lli = ll_i2info(inode);
	struct ll_sb_info *sbi = ll_i2sbi(inode);
	int rc;

	*gen = ll_layout_version_get(lli);
	if (!(sbi->ll_flags & LL_SBI_LAYOUT_LOCK) || *gen != CL_LAYOUT_GEN_NONE)
		return 0;

	/* sanity checks */
	LASSERT(fid_is_sane(ll_inode2fid(inode)));
	LASSERT(S_ISREG(inode->i_mode));

	/* take layout lock mutex to enqueue layout lock exclusively. */
	mutex_lock(&lli->lli_layout_mutex);

	rc = ll_layout_refresh_locked(inode);
	if (rc < 0)
		goto out;

	*gen = ll_layout_version_get(lli);
out:
	mutex_unlock(&lli->lli_layout_mutex);

	return rc;
}

/**
 *  This function send a restore request to the MDT
 */
int ll_layout_restore(struct inode *inode, loff_t offset, __u64 length)
{
	struct hsm_user_request	*hur;
	int			 len, rc;

	len = sizeof(struct hsm_user_request) +
	      sizeof(struct hsm_user_item);
	hur = kzalloc(len, GFP_NOFS);
	if (!hur)
		return -ENOMEM;

	hur->hur_request.hr_action = HUA_RESTORE;
	hur->hur_request.hr_archive_id = 0;
	hur->hur_request.hr_flags = 0;
	memcpy(&hur->hur_user_item[0].hui_fid, &ll_i2info(inode)->lli_fid,
	       sizeof(hur->hur_user_item[0].hui_fid));
	hur->hur_user_item[0].hui_extent.offset = offset;
	hur->hur_user_item[0].hui_extent.length = length;
	hur->hur_request.hr_itemcount = 1;
	rc = obd_iocontrol(LL_IOC_HSM_REQUEST, ll_i2sbi(inode)->ll_md_exp,
			   len, hur, NULL);
	kfree(hur);
	return rc;
}
