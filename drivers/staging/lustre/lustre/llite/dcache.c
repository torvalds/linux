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
#include <linux/sched.h>
#include <linux/quotaops.h>

#define DEBUG_SUBSYSTEM S_LLITE

#include "../include/obd_support.h"
#include "../include/lustre_lite.h"
#include "../include/lustre/lustre_idl.h"
#include "../include/lustre_dlm.h"

#include "llite_internal.h"

static void free_dentry_data(struct rcu_head *head)
{
	struct ll_dentry_data *lld;

	lld = container_of(head, struct ll_dentry_data, lld_rcu_head);
	OBD_FREE_PTR(lld);
}

/* should NOT be called with the dcache lock, see fs/dcache.c */
static void ll_release(struct dentry *de)
{
	struct ll_dentry_data *lld;

	LASSERT(de != NULL);
	lld = ll_d2d(de);
	if (lld == NULL) /* NFS copies the de->d_op methods (bug 4655) */
		return;

	if (lld->lld_it) {
		ll_intent_release(lld->lld_it);
		OBD_FREE(lld->lld_it, sizeof(*lld->lld_it));
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
 * lock_match() fails) and we spin around real_lookup(). */
static int ll_dcompare(const struct dentry *parent, const struct dentry *dentry,
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

static inline int return_if_equal(struct ldlm_lock *lock, void *data)
{
	if ((lock->l_flags &
	     (LDLM_FL_CANCELING | LDLM_FL_DISCARD_DATA)) ==
	    (LDLM_FL_CANCELING | LDLM_FL_DISCARD_DATA))
		return LDLM_ITER_CONTINUE;
	return LDLM_ITER_STOP;
}

/* find any ldlm lock of the inode in mdc and lov
 * return 0    not find
 *	1    find one
 *      < 0    error */
static int find_cbdata(struct inode *inode)
{
	struct ll_sb_info *sbi = ll_i2sbi(inode);
	struct lov_stripe_md *lsm;
	int rc = 0;

	LASSERT(inode);
	rc = md_find_cbdata(sbi->ll_md_exp, ll_inode2fid(inode),
			    return_if_equal, NULL);
	if (rc != 0)
		 return rc;

	lsm = ccc_inode_lsm_get(inode);
	if (lsm == NULL)
		return rc;

	rc = obd_find_cbdata(sbi->ll_dt_exp, lsm, return_if_equal, NULL);
	ccc_inode_lsm_put(inode, lsm);

	return rc;
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
	       de, de, de->d_parent, de->d_inode,
	       d_unhashed(de) ? "" : "hashed,",
	       list_empty(&de->d_subdirs) ? "" : "subdirs");

	/* kernel >= 2.6.38 last refcount is decreased after this function. */
	LASSERT(d_count(de) == 1);

	/* Disable this piece of code temporarily because this is called
	 * inside dcache_lock so it's not appropriate to do lots of work
	 * here. ATTENTION: Before this piece of code enabling, LU-2487 must be
	 * resolved. */
#if 0
	/* if not ldlm lock for this inode, set i_nlink to 0 so that
	 * this inode can be recycled later b=20433 */
	if (de->d_inode && !find_cbdata(de->d_inode))
		clear_nlink(de->d_inode);
#endif

	if (d_lustre_invalid((struct dentry *)de))
		return 1;
	return 0;
}

int ll_d_init(struct dentry *de)
{
	LASSERT(de != NULL);

	CDEBUG(D_DENTRY, "ldd on dentry %pd (%p) parent %p inode %p refc %d\n",
		de, de, de->d_parent, de->d_inode,
		d_count(de));

	if (de->d_fsdata == NULL) {
		struct ll_dentry_data *lld;

		lld = kzalloc(sizeof(*lld), GFP_NOFS);
		if (likely(lld)) {
			spin_lock(&de->d_lock);
			if (likely(de->d_fsdata == NULL)) {
				de->d_fsdata = lld;
				__d_lustre_invalidate(de);
			} else {
				OBD_FREE_PTR(lld);
			}
			spin_unlock(&de->d_lock);
		} else {
			return -ENOMEM;
		}
	}
	LASSERT(de->d_op == &ll_d_ops);

	return 0;
}

void ll_intent_drop_lock(struct lookup_intent *it)
{
	if (it->it_op && it->d.lustre.it_lock_mode) {
		struct lustre_handle handle;

		handle.cookie = it->d.lustre.it_lock_handle;

		CDEBUG(D_DLMTRACE, "releasing lock with cookie %#llx from it %p\n",
		       handle.cookie, it);
		ldlm_lock_decref(&handle, it->d.lustre.it_lock_mode);

		/* bug 494: intent_release may be called multiple times, from
		 * this thread and we don't want to double-decref this lock */
		it->d.lustre.it_lock_mode = 0;
		if (it->d.lustre.it_remote_lock_mode != 0) {
			handle.cookie = it->d.lustre.it_remote_lock_handle;

			CDEBUG(D_DLMTRACE, "releasing remote lock with cookie%#llx from it %p\n",
			       handle.cookie, it);
			ldlm_lock_decref(&handle,
					 it->d.lustre.it_remote_lock_mode);
			it->d.lustre.it_remote_lock_mode = 0;
		}
	}
}

void ll_intent_release(struct lookup_intent *it)
{
	CDEBUG(D_INFO, "intent %p released\n", it);
	ll_intent_drop_lock(it);
	/* We are still holding extra reference on a request, need to free it */
	if (it_disposition(it, DISP_ENQ_OPEN_REF))
		ptlrpc_req_finished(it->d.lustre.it_data); /* ll_file_open */

	if (it_disposition(it, DISP_ENQ_CREATE_REF)) /* create rec */
		ptlrpc_req_finished(it->d.lustre.it_data);

	it->d.lustre.it_disposition = 0;
	it->d.lustre.it_data = NULL;
}

void ll_invalidate_aliases(struct inode *inode)
{
	struct dentry *dentry;
	struct ll_d_hlist_node *p;

	LASSERT(inode != NULL);

	CDEBUG(D_INODE, "marking dentries for ino %lu/%u(%p) invalid\n",
	       inode->i_ino, inode->i_generation, inode);

	ll_lock_dcache(inode);
	ll_d_hlist_for_each_entry(dentry, p, &inode->i_dentry, d_u.d_alias) {
		CDEBUG(D_DENTRY, "dentry in drop %pd (%p) parent %p "
		       "inode %p flags %d\n", dentry, dentry, dentry->d_parent,
		       dentry->d_inode, dentry->d_flags);

		if (unlikely(dentry == dentry->d_sb->s_root)) {
			CERROR("%s: called on root dentry=%p, fid="DFID"\n",
			       ll_get_fsname(dentry->d_sb, NULL, 0),
			       dentry, PFID(ll_inode2fid(inode)));
			lustre_dump_dentry(dentry, 1);
			dump_stack();
		}

		d_lustre_invalidate(dentry, 0);
	}
	ll_unlock_dcache(inode);
}

int ll_revalidate_it_finish(struct ptlrpc_request *request,
			    struct lookup_intent *it,
			    struct dentry *de)
{
	int rc = 0;

	if (!request)
		return 0;

	if (it_disposition(it, DISP_LOOKUP_NEG))
		return -ENOENT;

	rc = ll_prep_inode(&de->d_inode, request, NULL, it);

	return rc;
}

void ll_lookup_finish_locks(struct lookup_intent *it, struct dentry *dentry)
{
	LASSERT(it != NULL);
	LASSERT(dentry != NULL);

	if (it->d.lustre.it_lock_mode && dentry->d_inode != NULL) {
		struct inode *inode = dentry->d_inode;
		struct ll_sb_info *sbi = ll_i2sbi(dentry->d_inode);

		CDEBUG(D_DLMTRACE, "setting l_data to inode %p (%lu/%u)\n",
		       inode, inode->i_ino, inode->i_generation);
		ll_set_lock_data(sbi->ll_md_exp, inode, it, NULL);
	}

	/* drop lookup or getattr locks immediately */
	if (it->it_op == IT_LOOKUP || it->it_op == IT_GETATTR) {
		/* on 2.6 there are situation when several lookups and
		 * revalidations may be requested during single operation.
		 * therefore, we don't release intent here -bzzz */
		ll_intent_drop_lock(it);
	}
}

static int ll_revalidate_dentry(struct dentry *dentry,
				unsigned int lookup_flags)
{
	struct inode *dir = dentry->d_parent->d_inode;

	/*
	 * if open&create is set, talk to MDS to make sure file is created if
	 * necessary, because we can't do this in ->open() later since that's
	 * called on an inode. return 0 here to let lookup to handle this.
	 */
	if ((lookup_flags & (LOOKUP_OPEN | LOOKUP_CREATE)) ==
	    (LOOKUP_OPEN | LOOKUP_CREATE))
		return 0;

	if (lookup_flags & (LOOKUP_PARENT | LOOKUP_OPEN | LOOKUP_CREATE))
		return 1;

	if (d_need_statahead(dir, dentry) <= 0)
		return 1;

	if (lookup_flags & LOOKUP_RCU)
		return -ECHILD;

	do_statahead_enter(dir, &dentry, dentry->d_inode == NULL);
	ll_statahead_mark(dir, dentry);
	return 1;
}

/*
 * Always trust cached dentries. Update statahead window if necessary.
 */
static int ll_revalidate_nd(struct dentry *dentry, unsigned int flags)
{
	int rc;

	CDEBUG(D_VFSTRACE, "VFS Op:name=%pd, flags=%u\n",
	       dentry, flags);

	rc = ll_revalidate_dentry(dentry, flags);
	return rc;
}


static void ll_d_iput(struct dentry *de, struct inode *inode)
{
	LASSERT(inode);
	if (!find_cbdata(inode))
		clear_nlink(inode);
	iput(inode);
}

const struct dentry_operations ll_d_ops = {
	.d_revalidate = ll_revalidate_nd,
	.d_release = ll_release,
	.d_delete  = ll_ddelete,
	.d_iput    = ll_d_iput,
	.d_compare = ll_dcompare,
};
