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

#include <lustre_lite.h>
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
		 * printing just the last part of the symlink. */
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
			CERROR("inode %lu: rc = %d\n", inode->i_ino, rc);
		GOTO (failed, rc);
	}

	body = req_capsule_server_get(&(*request)->rq_pill, &RMF_MDT_BODY);
	LASSERT(body != NULL);
	if ((body->valid & OBD_MD_LINKNAME) == 0) {
		CERROR("OBD_MD_LINKNAME not set on reply\n");
		GOTO(failed, rc = -EPROTO);
	}

	LASSERT(symlen != 0);
	if (body->eadatasize != symlen) {
		CERROR("inode %lu: symlink length %d not expected %d\n",
			inode->i_ino, body->eadatasize - 1, symlen - 1);
		GOTO(failed, rc = -EPROTO);
	}

	*symname = req_capsule_server_get(&(*request)->rq_pill, &RMF_MDT_MD);
	if (*symname == NULL ||
	    strnlen(*symname, symlen) != symlen - 1) {
		/* not full/NULL terminated */
		CERROR("inode %lu: symlink not NULL terminated string"
			"of length %d\n", inode->i_ino, symlen - 1);
		GOTO(failed, rc = -EPROTO);
	}

	OBD_ALLOC(lli->lli_symlink_name, symlen);
	/* do not return an error if we cannot cache the symlink locally */
	if (lli->lli_symlink_name) {
		memcpy(lli->lli_symlink_name, *symname, symlen);
		*symname = lli->lli_symlink_name;
	}
	return 0;

failed:
	return rc;
}

static int ll_readlink(struct dentry *dentry, char *buffer, int buflen)
{
	struct inode *inode = dentry->d_inode;
	struct ptlrpc_request *request;
	char *symname;
	int rc;

	CDEBUG(D_VFSTRACE, "VFS Op\n");

	ll_inode_size_lock(inode);
	rc = ll_readlink_internal(inode, &request, &symname);
	if (rc)
		GOTO(out, rc);

	rc = vfs_readlink(dentry, buffer, buflen, symname);
 out:
	ptlrpc_req_finished(request);
	ll_inode_size_unlock(inode);
	return rc;
}

static void *ll_follow_link(struct dentry *dentry, struct nameidata *nd)
{
	struct inode *inode = dentry->d_inode;
	struct ptlrpc_request *request = NULL;
	int rc;
	char *symname;

	CDEBUG(D_VFSTRACE, "VFS Op\n");
	/* Limit the recursive symlink depth to 5 instead of default
	 * 8 links when kernel has 4k stack to prevent stack overflow.
	 * For 8k stacks we need to limit it to 7 for local servers. */
	if (THREAD_SIZE < 8192 && current->link_count >= 6) {
		rc = -ELOOP;
	} else if (THREAD_SIZE == 8192 && current->link_count >= 8) {
		rc = -ELOOP;
	} else {
		ll_inode_size_lock(inode);
		rc = ll_readlink_internal(inode, &request, &symname);
		ll_inode_size_unlock(inode);
	}
	if (rc) {
		ptlrpc_req_finished(request);
		request = NULL;
		symname = ERR_PTR(rc);
	}

	nd_set_link(nd, symname);
	/* symname may contain a pointer to the request message buffer,
	 * we delay request releasing until ll_put_link then.
	 */
	return request;
}

static void ll_put_link(struct dentry *dentry, struct nameidata *nd, void *cookie)
{
	ptlrpc_req_finished(cookie);
}

struct inode_operations ll_fast_symlink_inode_operations = {
	.readlink	= ll_readlink,
	.setattr	= ll_setattr,
	.follow_link	= ll_follow_link,
	.put_link	= ll_put_link,
	.getattr	= ll_getattr,
	.permission	= ll_inode_permission,
	.setxattr	= ll_setxattr,
	.getxattr	= ll_getxattr,
	.listxattr	= ll_listxattr,
	.removexattr	= ll_removexattr,
};
