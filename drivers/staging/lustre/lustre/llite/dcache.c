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

#include <obd_support.h>
#include <lustre_lite.h>
#include <lustre/lustre_idl.h>
#include <lustre_dlm.h>

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
	ENTRY;
	LASSERT(de != NULL);
	lld = ll_d2d(de);
	if (lld == NULL) /* NFS copies the de->d_op methods (bug 4655) */
		RETURN_EXIT;

	if (lld->lld_it) {
		ll_intent_release(lld->lld_it);
		OBD_FREE(lld->lld_it, sizeof(*lld->lld_it));
	}
	LASSERT(lld->lld_cwd_count == 0);
	LASSERT(lld->lld_mnt_count == 0);
	de->d_fsdata = NULL;
	call_rcu(&lld->lld_rcu_head, free_dentry_data);

	EXIT;
}

/* Compare if two dentries are the same.  Don't match if the existing dentry
 * is marked invalid.  Returns 1 if different, 0 if the same.
 *
 * This avoids a race where ll_lookup_it() instantiates a dentry, but we get
 * an AST before calling d_revalidate_it().  The dentry still exists (marked
 * INVALID) so d_lookup() matches it, but we have no lock on it (so
 * lock_match() fails) and we spin around real_lookup(). */
int ll_dcompare(const struct dentry *parent, const struct inode *pinode,
		const struct dentry *dentry, const struct inode *inode,
		unsigned int len, const char *str, const struct qstr *name)
{
	ENTRY;

	if (len != name->len)
		RETURN(1);

	if (memcmp(str, name->name, len))
		RETURN(1);

	CDEBUG(D_DENTRY, "found name %.*s(%p) flags %#x refc %d\n",
	       name->len, name->name, dentry, dentry->d_flags,
	       d_count(dentry));

	/* mountpoint is always valid */
	if (d_mountpoint((struct dentry *)dentry))
		RETURN(0);

	if (d_lustre_invalid(dentry))
		RETURN(1);

	RETURN(0);
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
	ENTRY;

	LASSERT(inode);
	rc = md_find_cbdata(sbi->ll_md_exp, ll_inode2fid(inode),
			    return_if_equal, NULL);
	if (rc != 0)
		 RETURN(rc);

	lsm = ccc_inode_lsm_get(inode);
	if (lsm == NULL)
		RETURN(rc);

	rc = obd_find_cbdata(sbi->ll_dt_exp, lsm, return_if_equal, NULL);
	ccc_inode_lsm_put(inode, lsm);

	RETURN(rc);
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
	ENTRY;
	LASSERT(de);

	CDEBUG(D_DENTRY, "%s dentry %.*s (%p, parent %p, inode %p) %s%s\n",
	       d_lustre_invalid((struct dentry *)de) ? "deleting" : "keeping",
	       de->d_name.len, de->d_name.name, de, de->d_parent, de->d_inode,
	       d_unhashed((struct dentry *)de) ? "" : "hashed,",
	       list_empty(&de->d_subdirs) ? "" : "subdirs");

	/* kernel >= 2.6.38 last refcount is decreased after this function. */
	LASSERT(d_count(de) == 1);

	/* Disable this piece of code temproarily because this is called
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
		RETURN(1);
	RETURN(0);
}

static int ll_set_dd(struct dentry *de)
{
	ENTRY;
	LASSERT(de != NULL);

	CDEBUG(D_DENTRY, "ldd on dentry %.*s (%p) parent %p inode %p refc %d\n",
		de->d_name.len, de->d_name.name, de, de->d_parent, de->d_inode,
		d_count(de));

	if (de->d_fsdata == NULL) {
		struct ll_dentry_data *lld;

		OBD_ALLOC_PTR(lld);
		if (likely(lld != NULL)) {
			spin_lock(&de->d_lock);
			if (likely(de->d_fsdata == NULL))
				de->d_fsdata = lld;
			else
				OBD_FREE_PTR(lld);
			spin_unlock(&de->d_lock);
		} else {
			RETURN(-ENOMEM);
		}
	}

	RETURN(0);
}

int ll_dops_init(struct dentry *de, int block, int init_sa)
{
	struct ll_dentry_data *lld = ll_d2d(de);
	int rc = 0;

	if (lld == NULL && block != 0) {
		rc = ll_set_dd(de);
		if (rc)
			return rc;

		lld = ll_d2d(de);
	}

	if (lld != NULL && init_sa != 0)
		lld->lld_sa_generation = 0;

	/* kernel >= 2.6.38 d_op is set in d_alloc() */
	LASSERT(de->d_op == &ll_d_ops);
	return rc;
}

void ll_intent_drop_lock(struct lookup_intent *it)
{
	if (it->it_op && it->d.lustre.it_lock_mode) {
		struct lustre_handle handle;

		handle.cookie = it->d.lustre.it_lock_handle;

		CDEBUG(D_DLMTRACE, "releasing lock with cookie "LPX64
		       " from it %p\n", handle.cookie, it);
		ldlm_lock_decref(&handle, it->d.lustre.it_lock_mode);

		/* bug 494: intent_release may be called multiple times, from
		 * this thread and we don't want to double-decref this lock */
		it->d.lustre.it_lock_mode = 0;
		if (it->d.lustre.it_remote_lock_mode != 0) {
			handle.cookie = it->d.lustre.it_remote_lock_handle;

			CDEBUG(D_DLMTRACE, "releasing remote lock with cookie"
			       LPX64" from it %p\n", handle.cookie, it);
			ldlm_lock_decref(&handle,
					 it->d.lustre.it_remote_lock_mode);
			it->d.lustre.it_remote_lock_mode = 0;
		}
	}
}

void ll_intent_release(struct lookup_intent *it)
{
	ENTRY;

	CDEBUG(D_INFO, "intent %p released\n", it);
	ll_intent_drop_lock(it);
	/* We are still holding extra reference on a request, need to free it */
	if (it_disposition(it, DISP_ENQ_OPEN_REF))
		 ptlrpc_req_finished(it->d.lustre.it_data); /* ll_file_open */
	if (it_disposition(it, DISP_ENQ_CREATE_REF)) /* create rec */
		ptlrpc_req_finished(it->d.lustre.it_data);
	if (it_disposition(it, DISP_ENQ_COMPLETE)) /* saved req from revalidate
						    * to lookup */
		ptlrpc_req_finished(it->d.lustre.it_data);

	it->d.lustre.it_disposition = 0;
	it->d.lustre.it_data = NULL;
	EXIT;
}

void ll_invalidate_aliases(struct inode *inode)
{
	struct dentry *dentry;
	struct ll_d_hlist_node *p;
	ENTRY;

	LASSERT(inode != NULL);

	CDEBUG(D_INODE, "marking dentries for ino %lu/%u(%p) invalid\n",
	       inode->i_ino, inode->i_generation, inode);

	ll_lock_dcache(inode);
	ll_d_hlist_for_each_entry(dentry, p, &inode->i_dentry, d_alias) {
		CDEBUG(D_DENTRY, "dentry in drop %.*s (%p) parent %p "
		       "inode %p flags %d\n", dentry->d_name.len,
		       dentry->d_name.name, dentry, dentry->d_parent,
		       dentry->d_inode, dentry->d_flags);

		if (dentry->d_name.len == 1 && dentry->d_name.name[0] == '/') {
			CERROR("called on root (?) dentry=%p, inode=%p "
			       "ino=%lu\n", dentry, inode, inode->i_ino);
			lustre_dump_dentry(dentry, 1);
			libcfs_debug_dumpstack(NULL);
		}

		d_lustre_invalidate(dentry, 0);
	}
	ll_unlock_dcache(inode);

	EXIT;
}

int ll_revalidate_it_finish(struct ptlrpc_request *request,
			    struct lookup_intent *it,
			    struct dentry *de)
{
	int rc = 0;
	ENTRY;

	if (!request)
		RETURN(0);

	if (it_disposition(it, DISP_LOOKUP_NEG))
		RETURN(-ENOENT);

	rc = ll_prep_inode(&de->d_inode, request, NULL, it);

	RETURN(rc);
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

void ll_frob_intent(struct lookup_intent **itp, struct lookup_intent *deft)
{
	struct lookup_intent *it = *itp;

	if (!it || it->it_op == IT_GETXATTR)
		it = *itp = deft;

}

int ll_revalidate_it(struct dentry *de, int lookup_flags,
		     struct lookup_intent *it)
{
	struct md_op_data *op_data;
	struct ptlrpc_request *req = NULL;
	struct lookup_intent lookup_it = { .it_op = IT_LOOKUP };
	struct obd_export *exp;
	struct inode *parent = de->d_parent->d_inode;
	int rc;

	ENTRY;
	CDEBUG(D_VFSTRACE, "VFS Op:name=%s,intent=%s\n", de->d_name.name,
	       LL_IT2STR(it));

	if (de->d_inode == NULL) {
		__u64 ibits;

		/* We can only use negative dentries if this is stat or lookup,
		   for opens and stuff we do need to query server. */
		/* If there is IT_CREAT in intent op set, then we must throw
		   away this negative dentry and actually do the request to
		   kernel to create whatever needs to be created (if possible)*/
		if (it && (it->it_op & IT_CREAT))
			RETURN(0);

		if (d_lustre_invalid(de))
			RETURN(0);

		ibits = MDS_INODELOCK_UPDATE;
		rc = ll_have_md_lock(parent, &ibits, LCK_MINMODE);
		GOTO(out_sa, rc);
	}

	/* Never execute intents for mount points.
	 * Attributes will be fixed up in ll_inode_revalidate_it */
	if (d_mountpoint(de))
		GOTO(out_sa, rc = 1);

	/* need to get attributes in case root got changed from other client */
	if (de == de->d_sb->s_root) {
		rc = __ll_inode_revalidate_it(de, it, MDS_INODELOCK_LOOKUP);
		if (rc == 0)
			rc = 1;
		GOTO(out_sa, rc);
	}

	exp = ll_i2mdexp(de->d_inode);

	OBD_FAIL_TIMEOUT(OBD_FAIL_MDC_REVALIDATE_PAUSE, 5);
	ll_frob_intent(&it, &lookup_it);
	LASSERT(it);

	if (it->it_op == IT_LOOKUP && !d_lustre_invalid(de))
		RETURN(1);

	if (it->it_op == IT_OPEN) {
		struct inode *inode = de->d_inode;
		struct ll_inode_info *lli = ll_i2info(inode);
		struct obd_client_handle **och_p;
		__u64 *och_usecount;
		__u64 ibits;

		/*
		 * We used to check for MDS_INODELOCK_OPEN here, but in fact
		 * just having LOOKUP lock is enough to justify inode is the
		 * same. And if inode is the same and we have suitable
		 * openhandle, then there is no point in doing another OPEN RPC
		 * just to throw away newly received openhandle.  There are no
		 * security implications too, if file owner or access mode is
		 * change, LOOKUP lock is revoked.
		 */


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
		/* Check for the proper lock. */
		ibits = MDS_INODELOCK_LOOKUP;
		if (!ll_have_md_lock(inode, &ibits, LCK_MINMODE))
			goto do_lock;
		mutex_lock(&lli->lli_och_mutex);
		if (*och_p) { /* Everything is open already, do nothing */
			/*(*och_usecount)++;  Do not let them steal our open
			  handle from under us */
			SET_BUT_UNUSED(och_usecount);
			/* XXX The code above was my original idea, but in case
			   we have the handle, but we cannot use it due to later
			   checks (e.g. O_CREAT|O_EXCL flags set), nobody
			   would decrement counter increased here. So we just
			   hope the lock won't be invalidated in between. But
			   if it would be, we'll reopen the open request to
			   MDS later during file open path */
			mutex_unlock(&lli->lli_och_mutex);
			RETURN(1);
		} else {
			mutex_unlock(&lli->lli_och_mutex);
		}
	}

	if (it->it_op == IT_GETATTR) {
		rc = ll_statahead_enter(parent, &de, 0);
		if (rc == 1)
			goto mark;
		else if (rc != -EAGAIN && rc != 0)
			GOTO(out, rc = 0);
	}

do_lock:
	op_data = ll_prep_md_op_data(NULL, parent, de->d_inode,
				     de->d_name.name, de->d_name.len,
				     0, LUSTRE_OPC_ANY, NULL);
	if (IS_ERR(op_data))
		RETURN(PTR_ERR(op_data));

	if (!IS_POSIXACL(parent) || !exp_connect_umask(exp))
		it->it_create_mode &= ~current_umask();
	it->it_create_mode |= M_CHECK_STALE;
	rc = md_intent_lock(exp, op_data, NULL, 0, it,
			    lookup_flags,
			    &req, ll_md_blocking_ast, 0);
	it->it_create_mode &= ~M_CHECK_STALE;
	ll_finish_md_op_data(op_data);

	/* If req is NULL, then md_intent_lock only tried to do a lock match;
	 * if all was well, it will return 1 if it found locks, 0 otherwise. */
	if (req == NULL && rc >= 0) {
		if (!rc)
			goto do_lookup;
		GOTO(out, rc);
	}

	if (rc < 0) {
		if (rc != -ESTALE) {
			CDEBUG(D_INFO, "ll_intent_lock: rc %d : it->it_status "
			       "%d\n", rc, it->d.lustre.it_status);
		}
		GOTO(out, rc = 0);
	}

revalidate_finish:
	rc = ll_revalidate_it_finish(req, it, de);
	if (rc != 0) {
		if (rc != -ESTALE && rc != -ENOENT)
			ll_intent_release(it);
		GOTO(out, rc = 0);
	}

	if ((it->it_op & IT_OPEN) && de->d_inode &&
	    !S_ISREG(de->d_inode->i_mode) &&
	    !S_ISDIR(de->d_inode->i_mode)) {
		ll_release_openhandle(de, it);
	}
	rc = 1;

out:
	/* We do not free request as it may be reused during following lookup
	 * (see comment in mdc/mdc_locks.c::mdc_intent_lock()), request will
	 * be freed in ll_lookup_it or in ll_intent_release. But if
	 * request was not completed, we need to free it. (bug 5154, 9903) */
	if (req != NULL && !it_disposition(it, DISP_ENQ_COMPLETE))
		ptlrpc_req_finished(req);
	if (rc == 0) {
		/* mdt may grant layout lock for the newly created file, so
		 * release the lock to avoid leaking */
		ll_intent_drop_lock(it);
		ll_invalidate_aliases(de->d_inode);
	} else {
		__u64 bits = 0;
		__u64 matched_bits = 0;

		CDEBUG(D_DENTRY, "revalidated dentry %.*s (%p) parent %p "
		       "inode %p refc %d\n", de->d_name.len,
		       de->d_name.name, de, de->d_parent, de->d_inode,
		       d_count(de));

		ll_set_lock_data(exp, de->d_inode, it, &bits);

		/* Note: We have to match both LOOKUP and PERM lock
		 * here to make sure the dentry is valid and no one
		 * changing the permission.
		 * But if the client connects < 2.4 server, which will
		 * only grant LOOKUP lock, so we can only Match LOOKUP
		 * lock for old server */
		if (exp_connect_flags(ll_i2mdexp(de->d_inode)) &&
							OBD_CONNECT_LVB_TYPE)
			matched_bits =
				MDS_INODELOCK_LOOKUP | MDS_INODELOCK_PERM;
		else
			matched_bits = MDS_INODELOCK_LOOKUP;

		if (((bits & matched_bits) == matched_bits) &&
		    d_lustre_invalid(de))
			d_lustre_revalidate(de);
		ll_lookup_finish_locks(it, de);
	}

mark:
	if (it != NULL && it->it_op == IT_GETATTR && rc > 0)
		ll_statahead_mark(parent, de);
	RETURN(rc);

	/*
	 * This part is here to combat evil-evil race in real_lookup on 2.6
	 * kernels.  The race details are: We enter do_lookup() looking for some
	 * name, there is nothing in dcache for this name yet and d_lookup()
	 * returns NULL.  We proceed to real_lookup(), and while we do this,
	 * another process does open on the same file we looking up (most simple
	 * reproducer), open succeeds and the dentry is added. Now back to
	 * us. In real_lookup() we do d_lookup() again and suddenly find the
	 * dentry, so we call d_revalidate on it, but there is no lock, so
	 * without this code we would return 0, but unpatched real_lookup just
	 * returns -ENOENT in such a case instead of retrying the lookup. Once
	 * this is dealt with in real_lookup(), all of this ugly mess can go and
	 * we can just check locks in ->d_revalidate without doing any RPCs
	 * ever.
	 */
do_lookup:
	if (it != &lookup_it) {
		/* MDS_INODELOCK_UPDATE needed for IT_GETATTR case. */
		if (it->it_op == IT_GETATTR)
			lookup_it.it_op = IT_GETATTR;
		ll_lookup_finish_locks(it, de);
		it = &lookup_it;
	}

	/* Do real lookup here. */
	op_data = ll_prep_md_op_data(NULL, parent, NULL, de->d_name.name,
				     de->d_name.len, 0, (it->it_op & IT_CREAT ?
							 LUSTRE_OPC_CREATE :
							 LUSTRE_OPC_ANY), NULL);
	if (IS_ERR(op_data))
		RETURN(PTR_ERR(op_data));

	rc = md_intent_lock(exp, op_data, NULL, 0,  it, 0, &req,
			    ll_md_blocking_ast, 0);
	if (rc >= 0) {
		struct mdt_body *mdt_body;
		struct lu_fid fid = {.f_seq = 0, .f_oid = 0, .f_ver = 0};
		mdt_body = req_capsule_server_get(&req->rq_pill, &RMF_MDT_BODY);

		if (de->d_inode)
			fid = *ll_inode2fid(de->d_inode);

		/* see if we got same inode, if not - return error */
		if (lu_fid_eq(&fid, &mdt_body->fid1)) {
			ll_finish_md_op_data(op_data);
			op_data = NULL;
			goto revalidate_finish;
		}
		ll_intent_release(it);
	}
	ll_finish_md_op_data(op_data);
	GOTO(out, rc = 0);

out_sa:
	/*
	 * For rc == 1 case, should not return directly to prevent losing
	 * statahead windows; for rc == 0 case, the "lookup" will be done later.
	 */
	if (it != NULL && it->it_op == IT_GETATTR && rc == 1)
		ll_statahead_enter(parent, &de, 1);
	goto mark;
}

/*
 * Always trust cached dentries. Update statahead window if necessary.
 */
int ll_revalidate_nd(struct dentry *dentry, unsigned int flags)
{
	struct inode *parent = dentry->d_parent->d_inode;
	int unplug = 0;

	ENTRY;
	CDEBUG(D_VFSTRACE, "VFS Op:name=%s,flags=%u\n",
	       dentry->d_name.name, flags);

	if (!(flags & (LOOKUP_PARENT|LOOKUP_OPEN|LOOKUP_CREATE)) &&
	    ll_need_statahead(parent, dentry) > 0) {
		if (flags & LOOKUP_RCU)
			RETURN(-ECHILD);

		if (dentry->d_inode == NULL)
			unplug = 1;
		do_statahead_enter(parent, &dentry, unplug);
		ll_statahead_mark(parent, dentry);
	}

	RETURN(1);
}


void ll_d_iput(struct dentry *de, struct inode *inode)
{
	LASSERT(inode);
	if (!find_cbdata(inode))
		clear_nlink(inode);
	iput(inode);
}

struct dentry_operations ll_d_ops = {
	.d_revalidate = ll_revalidate_nd,
	.d_release = ll_release,
	.d_delete  = ll_ddelete,
	.d_iput    = ll_d_iput,
	.d_compare = ll_dcompare,
};
