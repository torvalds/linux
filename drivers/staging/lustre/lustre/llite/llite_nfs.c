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
 * Copyright (c) 2003, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lustre/lustre/llite/llite_nfs.c
 *
 * NFS export of Lustre Light File System
 *
 * Author: Yury Umanets <umka@clusterfs.com>
 * Author: Huang Hua <huanghua@clusterfs.com>
 */

#define DEBUG_SUBSYSTEM S_LLITE
#include "../include/lustre_lite.h"
#include "llite_internal.h"
#include <linux/exportfs.h>

__u32 get_uuid2int(const char *name, int len)
{
	__u32 key0 = 0x12a3fe2d, key1 = 0x37abe8f9;
	while (len--) {
		__u32 key = key1 + (key0 ^ (*name++ * 7152373));

		if (key & 0x80000000)
			key -= 0x7fffffff;
		key1 = key0;
		key0 = key;
	}
	return (key0 << 1);
}

void get_uuid2fsid(const char *name, int len, __kernel_fsid_t *fsid)
{
	__u64 key = 0, key0 = 0x12a3fe2d, key1 = 0x37abe8f9;

	while (len--) {
		key = key1 + (key0 ^ (*name++ * 7152373));
		if (key & 0x8000000000000000ULL)
			key -= 0x7fffffffffffffffULL;
		key1 = key0;
		key0 = key;
	}

	fsid->val[0] = key;
	fsid->val[1] = key >> 32;
}

static int ll_nfs_test_inode(struct inode *inode, void *opaque)
{
	return lu_fid_eq(&ll_i2info(inode)->lli_fid,
			 (struct lu_fid *)opaque);
}

struct inode *search_inode_for_lustre(struct super_block *sb,
				      const struct lu_fid *fid)
{
	struct ll_sb_info     *sbi = ll_s2sbi(sb);
	struct ptlrpc_request *req = NULL;
	struct inode	  *inode = NULL;
	int		   eadatalen = 0;
	unsigned long	      hash = cl_fid_build_ino(fid,
						      ll_need_32bit_api(sbi));
	struct  md_op_data    *op_data;
	int		   rc;

	CDEBUG(D_INFO, "searching inode for:(%lu,"DFID")\n", hash, PFID(fid));

	inode = ilookup5(sb, hash, ll_nfs_test_inode, (void *)fid);
	if (inode)
		return inode;

	rc = ll_get_default_mdsize(sbi, &eadatalen);
	if (rc)
		return ERR_PTR(rc);

	/* Because inode is NULL, ll_prep_md_op_data can not
	 * be used here. So we allocate op_data ourselves */
	op_data = kzalloc(sizeof(*op_data), GFP_NOFS);
	if (!op_data)
		return ERR_PTR(-ENOMEM);

	op_data->op_fid1 = *fid;
	op_data->op_mode = eadatalen;
	op_data->op_valid = OBD_MD_FLEASIZE;

	/* mds_fid2dentry ignores f_type */
	rc = md_getattr(sbi->ll_md_exp, op_data, &req);
	OBD_FREE_PTR(op_data);
	if (rc) {
		CERROR("can't get object attrs, fid "DFID", rc %d\n",
		       PFID(fid), rc);
		return ERR_PTR(rc);
	}
	rc = ll_prep_inode(&inode, req, sb, NULL);
	ptlrpc_req_finished(req);
	if (rc)
		return ERR_PTR(rc);

	return inode;
}

struct lustre_nfs_fid {
	struct lu_fid   lnf_child;
	struct lu_fid   lnf_parent;
};

static struct dentry *
ll_iget_for_nfs(struct super_block *sb, struct lu_fid *fid, struct lu_fid *parent)
{
	struct inode  *inode;
	struct dentry *result;

	CDEBUG(D_INFO, "Get dentry for fid: "DFID"\n", PFID(fid));
	if (!fid_is_sane(fid))
		return ERR_PTR(-ESTALE);

	inode = search_inode_for_lustre(sb, fid);
	if (IS_ERR(inode))
		return ERR_CAST(inode);

	if (is_bad_inode(inode)) {
		/* we didn't find the right inode.. */
		iput(inode);
		return ERR_PTR(-ESTALE);
	}

	/**
	 * It is an anonymous dentry without OST objects created yet.
	 * We have to find the parent to tell MDS how to init lov objects.
	 */
	if (S_ISREG(inode->i_mode) && !ll_i2info(inode)->lli_has_smd &&
	    parent != NULL) {
		struct ll_inode_info *lli = ll_i2info(inode);

		spin_lock(&lli->lli_lock);
		lli->lli_pfid = *parent;
		spin_unlock(&lli->lli_lock);
	}

	result = d_obtain_alias(inode);
	if (IS_ERR(result)) {
		iput(inode);
		return result;
	}

	return result;
}

#define LUSTRE_NFS_FID	  0x97

/**
 * \a connectable - is nfsd will connect himself or this should be done
 *		  at lustre
 *
 * The return value is file handle type:
 * 1 -- contains child file handle;
 * 2 -- contains child file handle and parent file handle;
 * 255 -- error.
 */
static int ll_encode_fh(struct inode *inode, __u32 *fh, int *plen,
			struct inode *parent)
{
	struct lustre_nfs_fid *nfs_fid = (void *)fh;

	CDEBUG(D_INFO, "encoding for (%lu,"DFID") maxlen=%d minlen=%d\n",
	      inode->i_ino, PFID(ll_inode2fid(inode)), *plen,
	      (int)sizeof(struct lustre_nfs_fid));

	if (*plen < sizeof(struct lustre_nfs_fid) / 4)
		return 255;

	nfs_fid->lnf_child = *ll_inode2fid(inode);
	nfs_fid->lnf_parent = *ll_inode2fid(parent);
	*plen = sizeof(struct lustre_nfs_fid) / 4;

	return LUSTRE_NFS_FID;
}

static int ll_nfs_get_name_filldir(void *cookie, const char *name, int namelen,
				   loff_t hash, u64 ino, unsigned type)
{
	/* It is hack to access lde_fid for comparison with lgd_fid.
	 * So the input 'name' must be part of the 'lu_dirent'. */
	struct lu_dirent *lde = container_of0(name, struct lu_dirent, lde_name);
	struct ll_getname_data *lgd = cookie;
	struct lu_fid fid;

	fid_le_to_cpu(&fid, &lde->lde_fid);
	if (lu_fid_eq(&fid, &lgd->lgd_fid)) {
		memcpy(lgd->lgd_name, name, namelen);
		lgd->lgd_name[namelen] = 0;
		lgd->lgd_found = 1;
	}
	return lgd->lgd_found;
}

static int ll_get_name(struct dentry *dentry, char *name,
		       struct dentry *child)
{
	struct inode *dir = dentry->d_inode;
	int rc;
	struct ll_getname_data lgd = {
		.lgd_name = name,
		.lgd_fid = ll_i2info(child->d_inode)->lli_fid,
		.ctx.actor = ll_nfs_get_name_filldir,
	};

	if (!dir || !S_ISDIR(dir->i_mode)) {
		rc = -ENOTDIR;
		goto out;
	}

	if (!dir->i_fop) {
		rc = -EINVAL;
		goto out;
	}

	mutex_lock(&dir->i_mutex);
	rc = ll_dir_read(dir, &lgd.ctx);
	mutex_unlock(&dir->i_mutex);
	if (!rc && !lgd.lgd_found)
		rc = -ENOENT;
out:
	return rc;
}

static struct dentry *ll_fh_to_dentry(struct super_block *sb, struct fid *fid,
				      int fh_len, int fh_type)
{
	struct lustre_nfs_fid *nfs_fid = (struct lustre_nfs_fid *)fid;

	if (fh_type != LUSTRE_NFS_FID)
		return ERR_PTR(-EPROTO);

	return ll_iget_for_nfs(sb, &nfs_fid->lnf_child, &nfs_fid->lnf_parent);
}

static struct dentry *ll_fh_to_parent(struct super_block *sb, struct fid *fid,
				      int fh_len, int fh_type)
{
	struct lustre_nfs_fid *nfs_fid = (struct lustre_nfs_fid *)fid;

	if (fh_type != LUSTRE_NFS_FID)
		return ERR_PTR(-EPROTO);

	return ll_iget_for_nfs(sb, &nfs_fid->lnf_parent, NULL);
}

static struct dentry *ll_get_parent(struct dentry *dchild)
{
	struct ptlrpc_request *req = NULL;
	struct inode	  *dir = dchild->d_inode;
	struct ll_sb_info     *sbi;
	struct dentry	 *result = NULL;
	struct mdt_body       *body;
	static char	   dotdot[] = "..";
	struct md_op_data     *op_data;
	int		   rc;
	int		      lmmsize;

	LASSERT(dir && S_ISDIR(dir->i_mode));

	sbi = ll_s2sbi(dir->i_sb);

	CDEBUG(D_INFO, "getting parent for (%lu,"DFID")\n",
			dir->i_ino, PFID(ll_inode2fid(dir)));

	rc = ll_get_default_mdsize(sbi, &lmmsize);
	if (rc != 0)
		return ERR_PTR(rc);

	op_data = ll_prep_md_op_data(NULL, dir, NULL, dotdot,
				     strlen(dotdot), lmmsize,
				     LUSTRE_OPC_ANY, NULL);
	if (IS_ERR(op_data))
		return (void *)op_data;

	rc = md_getattr_name(sbi->ll_md_exp, op_data, &req);
	ll_finish_md_op_data(op_data);
	if (rc) {
		CERROR("failure %d inode %lu get parent\n", rc, dir->i_ino);
		return ERR_PTR(rc);
	}
	body = req_capsule_server_get(&req->rq_pill, &RMF_MDT_BODY);
	LASSERT(body->valid & OBD_MD_FLID);

	CDEBUG(D_INFO, "parent for "DFID" is "DFID"\n",
		PFID(ll_inode2fid(dir)), PFID(&body->fid1));

	result = ll_iget_for_nfs(dir->i_sb, &body->fid1, NULL);

	ptlrpc_req_finished(req);
	return result;
}

struct export_operations lustre_export_operations = {
       .get_parent = ll_get_parent,
       .encode_fh  = ll_encode_fh,
       .get_name   = ll_get_name,
	.fh_to_dentry = ll_fh_to_dentry,
	.fh_to_parent = ll_fh_to_parent,
};
