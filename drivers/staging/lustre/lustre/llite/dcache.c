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
 */

#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/quotaops.h>

#define DEBUG_SUBSYSTEM S_LLITE

#include "../include/obd_support.h"
#include "../include/lustre/lustre_idl.h"
#include "../include/lustre_dlm.h"

#include "llite_internal.h"

static void free_dentry_data(struct rcu_head *head)
{
	struct ll_dentry_data *lld;

	lld = container_of(head, struct ll_dentry_data, lld_rcu_head);
	kfree(lld);
}

/* should NOT be called with the dcache lock, see fs/dcache.c */
static void ll_release(struct dentry *de)
{
	struct ll_dentry_data *lld;

	LASSERT(de);
	lld = ll_d2d(de);
	if (lld->lld_it) {
		ll_intent_release(lld->lld_it);
		kfree(lld->lld_it);
	}

	de->d_fsdata = NULL;
	call_rcu(&lld->lld_rcu_head, free_dentry_data);
}

/* Compare if two dentries are the same.  Don't match if the existing dentry
 * is marked invalid.  Returns 1 if different, 0 if the same.
 *
 * This avoids a race where ll_lookup_it() instantiates a dentry, but we get
 * an AST before calling d_revalidate_it().  The dentry still exists (marked
 * INVALID) so d_lookup() matches it, but we have no lock on it (so
 * lock_match() fails) and we spin around real_lookup().
 */
static int ll_dcompare(const struct dentry *dentry,
		       unsigned int len, const char *str,
		       const struct qstr *name)
{
	if (len != name->len)
		return 1;

	if (memcmp(str, name->name, len))
		return 1;

	CDEBUG(D_DENTRY, "found name %.*s(%p) flags %#x refc %d\n",
	       name->len, name->name, dentry, dentry->d_flags,
	       d_count(dentry));

	/* mountpoint is always valid */
	if (d_mountpoint((struct dentry *)dentry))
		return 0;

	if (d_lustre_invalid(dentry))
		return 1;

	return 0;
}

/**
 * Called when last reference to a dentry is dropped and dcache wants to know
 * whether or not it should cache it:
 * - return 1 to delete the dentry immediately
 * - return 0 to cache the dentry
 * Should NOT be called with the dcache lock, see fs/dcache.c
 */
static int ll_ddelete(const struct dentry *de)
{
	LASSERT(de);

	CDEBUG(D_DENTRY, "%s dentry %pd (%p, parent %p, inode %p) %s%s\n",
	       d_lustre_invalid((struct dentry *)de) ? "deleting" : "keeping",
	       de, de, de->d_parent, d_inode(de),
	       d_unhashed(de) ? "" : "hashed,",
	       list_empty(&de->d_subdirs) ? "" : "subdirs");

	/* kernel >= 2.6.38 last refcount is decreased after this function. */
	LASSERT(d_count(de) == 1);

	if (d_lustre_invalid((struct dentry *)de))
		return 1;
	return 0;
}

static int ll_d_init(struct dentry *de)
{
	struct ll_dentry_data *lld = kzalloc(sizeof(*lld), GFP_KERNEL);

	if (unlikely(!lld))
		return -ENOMEM;
	lld->lld_invalid = 1;
	de->d_fsdata = lld;
	return 0;
}

void ll_intent_drop_lock(struct lookup_intent *it)
{
	if (it->it_op && it->it_lock_mode) {
		struct lustre_handle handle;

		handle.cookie = it->it_lock_handle;

		CDEBUG(D_DLMTRACE, "releasing lock with cookie %#llx from it %p\n",
		       handle.cookie, it);
		ldlm_lock_decref(&handle, it->it_lock_mode);

		/* bug 494: intent_release may be called multiple times, from
		 * this thread and we don't want to double-decref this lock
		 */
		it->it_lock_mode = 0;
		if (it->it_remote_lock_mode != 0) {
			handle.cookie = it->it_remote_lock_handle;

			CDEBUG(D_DLMTRACE, "releasing remote lock with cookie%#llx from it %p\n",
			       handle.cookie, it);
			ldlm_lock_decref(&handle,
					 it->it_remote_lock_mode);
			it->it_remote_lock_mode = 0;
		}
	}
}

void ll_intent_release(struct lookup_intent *it)
{
	CDEBUG(D_INFO, "intent %p released\n", it);
	ll_intent_drop_lock(it);
	/* We are still holding extra reference on a request, need to free it */
	if (it_disposition(it, DISP_ENQ_OPEN_REF))
		ptlrpc_req_finished(it->it_request); /* ll_file_open */

	if (it_disposition(it, DISP_ENQ_CREATE_REF)) /* create rec */
		ptlrpc_req_finished(it->it_request);

	it->it_disposition = 0;
	it->it_request = NULL;
}

void ll_invalidate_aliases(struct inode *inode)
{
	struct dentry *dentry;

	CDEBUG(D_INODE, "marking dentries for ino "DFID"(%p) invalid\n",
	       PFID(ll_inode2fid(inode)), inode);

	spin_lock(&inode->i_lock);
	hlist_for_each_entry(dentry, &inode->i_dentry, d_u.d_alias) {
		CDEBUG(D_DENTRY, "dentry in drop %pd (%p) parent %p inode %p flags %d\n",
		       dentry, dentry, dentry->d_parent,
		       d_inode(dentry), dentry->d_flags);

		d_lustre_invalidate(dentry, 0);
	}
	spin_unlock(&inode->i_lock);
}

int ll_revalidate_it_finish(struct ptlrpc_request *request,
			    struct lookup_intent *it,
			    struct inode *inode)
{
	int rc = 0;

	if (!request)
		return 0;

	if (it_disposition(it, DISP_LOOKUP_NEG))
		return -ENOENT;

	rc = ll_prep_inode(&inode, request, NULL, it);

	return rc;
}

void ll_lookup_finish_locks(struct lookup_intent *it, struct inode *inode)
{
	if (it->it_lock_mode && inode) {
		struct ll_sb_info *sbi = ll_i2sbi(inode);

		CDEBUG(D_DLMTRACE, "setting l_data to inode "DFID"(%p)\n",
		       PFID(ll_inode2fid(inode)), inode);
		ll_set_lock_data(sbi->ll_md_exp, inode, it, NULL);
	}

	/* drop lookup or getattr locks immediately */
	if (it->it_op == IT_LOOKUP || it->it_op == IT_GETATTR) {
		/* on 2.6 there are situation when several lookups and
		 * revalidations may be requested during single operation.
		 * therefore, we don't release intent here -bzzz
		 */
		ll_intent_drop_lock(it);
	}
}

static int ll_revalidate_dentry(struct dentry *dentry,
				unsigned int lookup_flags)
{
	struct inode *dir = d_inode(dentry->d_parent);

	/* If this is intermediate component path lookup and we were able to get
	 * to this dentry, then its lock has not been revoked and the
	 * path component is valid.
	 */
	if (lookup_flags & LOOKUP_PARENT)
		return 1;

	/* Symlink - always valid as long as the dentry was found */
	if (dentry->d_inode && S_ISLNK(dentry->d_inode->i_mode))
		return 1;

	/*
	 * VFS warns us that this is the second go around and previous
	 * operation failed (most likely open|creat), so this time
	 * we better talk to the server via the lookup path by name,
	 * not by fid.
	 */
	if (lookup_flags & LOOKUP_REVAL)
		return 0;

	if (!dentry_may_statahead(dir, dentry))
		return 1;

	if (lookup_flags & LOOKUP_RCU)
		return -ECHILD;

	ll_statahead(dir, &dentry, !d_inode(dentry));
	return 1;
}

/*
 * Always trust cached dentries. Update statahead window if necessary.
 */
static int ll_revalidate_nd(struct dentry *dentry, unsigned int flags)
{
	CDEBUG(D_VFSTRACE, "VFS Op:name=%pd, flags=%u\n",
	       dentry, flags);

	return ll_revalidate_dentry(dentry, flags);
}

const struct dentry_operations ll_d_ops = {
	.d_init = ll_d_init,
	.d_revalidate = ll_revalidate_nd,
	.d_release = ll_release,
	.d_delete  = ll_ddelete,
	.d_compare = ll_dcompare,
};
