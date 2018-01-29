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
 * Copyright (c) 2011, 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 */

#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/stat.h>
#define DEBUG_SUBSYSTEM S_LLITE

#include "llite_internal.h"

static int ll_readlink_internal(struct inode *inode,
				struct ptlrpc_request **request, char **symname)
{
	struct ll_inode_info *lli = ll_i2info(inode);
	struct ll_sb_info *sbi = ll_i2sbi(inode);
	int rc, symlen = i_size_read(inode) + 1;
	struct mdt_body *body;
	struct md_op_data *op_data;

	*request = NULL;

	if (lli->lli_symlink_name) {
		int print_limit = min_t(int, PAGE_SIZE - 128, symlen);

		*symname = lli->lli_symlink_name;
		/* If the total CDEBUG() size is larger than a page, it
		 * will print a warning to the console, avoid this by
		 * printing just the last part of the symlink.
		 */
		CDEBUG(D_INODE, "using cached symlink %s%.*s, len = %d\n",
		       print_limit < symlen ? "..." : "", print_limit,
		       (*symname) + symlen - print_limit, symlen);
		return 0;
	}

	op_data = ll_prep_md_op_data(NULL, inode, NULL, NULL, 0, symlen,
				     LUSTRE_OPC_ANY, NULL);
	if (IS_ERR(op_data))
		return PTR_ERR(op_data);

	op_data->op_valid = OBD_MD_LINKNAME;
	rc = md_getattr(sbi->ll_md_exp, op_data, request);
	ll_finish_md_op_data(op_data);
	if (rc) {
		if (rc != -ENOENT)
			CERROR("%s: inode " DFID ": rc = %d\n",
			       ll_get_fsname(inode->i_sb, NULL, 0),
			       PFID(ll_inode2fid(inode)), rc);
		goto failed;
	}

	body = req_capsule_server_get(&(*request)->rq_pill, &RMF_MDT_BODY);
	if ((body->mbo_valid & OBD_MD_LINKNAME) == 0) {
		CERROR("OBD_MD_LINKNAME not set on reply\n");
		rc = -EPROTO;
		goto failed;
	}

	LASSERT(symlen != 0);
	if (body->mbo_eadatasize != symlen) {
		CERROR("%s: inode " DFID ": symlink length %d not expected %d\n",
		       ll_get_fsname(inode->i_sb, NULL, 0),
		       PFID(ll_inode2fid(inode)), body->mbo_eadatasize - 1,
		       symlen - 1);
		rc = -EPROTO;
		goto failed;
	}

	*symname = req_capsule_server_get(&(*request)->rq_pill, &RMF_MDT_MD);
	if (!*symname ||
	    strnlen(*symname, symlen) != symlen - 1) {
		/* not full/NULL terminated */
		CERROR("inode %lu: symlink not NULL terminated string of length %d\n",
		       inode->i_ino, symlen - 1);
		rc = -EPROTO;
		goto failed;
	}

	lli->lli_symlink_name = kzalloc(symlen, GFP_NOFS);
	/* do not return an error if we cannot cache the symlink locally */
	if (lli->lli_symlink_name) {
		memcpy(lli->lli_symlink_name, *symname, symlen);
		*symname = lli->lli_symlink_name;
	}
	return 0;

failed:
	return rc;
}

static void ll_put_link(void *p)
{
	ptlrpc_req_finished(p);
}

static const char *ll_get_link(struct dentry *dentry,
			       struct inode *inode,
			       struct delayed_call *done)
{
	struct ptlrpc_request *request = NULL;
	int rc;
	char *symname = NULL;

	if (!dentry)
		return ERR_PTR(-ECHILD);

	CDEBUG(D_VFSTRACE, "VFS Op\n");
	ll_inode_size_lock(inode);
	rc = ll_readlink_internal(inode, &request, &symname);
	ll_inode_size_unlock(inode);
	if (rc) {
		ptlrpc_req_finished(request);
		return ERR_PTR(rc);
	}

	/* symname may contain a pointer to the request message buffer,
	 * we delay request releasing then.
	 */
	set_delayed_call(done, ll_put_link, request);
	return symname;
}

const struct inode_operations ll_fast_symlink_inode_operations = {
	.setattr	= ll_setattr,
	.get_link	= ll_get_link,
	.getattr	= ll_getattr,
	.permission	= ll_inode_permission,
	.listxattr	= ll_listxattr,
};
