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
#include <linux/mm.h>
#include <linux/quotaops.h>
#include <linux/highmem.h>
#include <linux/pagemap.h>
#include <linux/security.h>

#define DEBUG_SUBSYSTEM S_LLITE

#include "../include/obd_support.h"
#include "../include/lustre_fid.h"
#include "../include/lustre_lite.h"
#include "../include/lustre_dlm.h"
#include "../include/lustre_ver.h"
#include "llite_internal.h"

static int ll_create_it(struct inode *, struct dentry *,
			int, struct lookup_intent *);

/*
 * Check if we have something mounted at the named dchild.
 * In such a case there would always be dentry present.
 */
static int ll_d_mountpoint(struct dentry *dparent, struct dentry *dchild,
			   struct qstr *name)
{
	int mounted = 0;

	if (unlikely(dchild)) {
		mounted = d_mountpoint(dchild);
	} else if (dparent) {
		dchild = d_lookup(dparent, name);
		if (dchild) {
			mounted = d_mountpoint(dchild);
			dput(dchild);
		}
	}
	return mounted;
}

/* called from iget5_locked->find_inode() under inode_hash_lock spinlock */
static int ll_test_inode(struct inode *inode, void *opaque)
{
	struct ll_inode_info *lli = ll_i2info(inode);
	struct lustre_md     *md = opaque;

	if (unlikely(!(md->body->valid & OBD_MD_FLID))) {
		CERROR("MDS body missing FID\n");
		return 0;
	}

	if (!lu_fid_eq(&lli->lli_fid, &md->body->fid1))
		return 0;

	return 1;
}

static int ll_set_inode(struct inode *inode, void *opaque)
{
	struct ll_inode_info *lli = ll_i2info(inode);
	struct mdt_body *body = ((struct lustre_md *)opaque)->body;

	if (unlikely(!(body->valid & OBD_MD_FLID))) {
		CERROR("MDS body missing FID\n");
		return -EINVAL;
	}

	lli->lli_fid = body->fid1;
	if (unlikely(!(body->valid & OBD_MD_FLTYPE))) {
		CERROR("Can not initialize inode "DFID" without object type: "
		       "valid = %#llx\n", PFID(&lli->lli_fid), body->valid);
		return -EINVAL;
	}

	inode->i_mode = (inode->i_mode & ~S_IFMT) | (body->mode & S_IFMT);
	if (unlikely(inode->i_mode == 0)) {
		CERROR("Invalid inode "DFID" type\n", PFID(&lli->lli_fid));
		return -EINVAL;
	}

	ll_lli_init(lli);

	return 0;
}


/*
 * Get an inode by inode number (already instantiated by the intent lookup).
 * Returns inode or NULL
 */
struct inode *ll_iget(struct super_block *sb, ino_t hash,
		      struct lustre_md *md)
{
	struct inode	 *inode;

	LASSERT(hash != 0);
	inode = iget5_locked(sb, hash, ll_test_inode, ll_set_inode, md);

	if (inode) {
		if (inode->i_state & I_NEW) {
			int rc = 0;

			ll_read_inode2(inode, md);
			if (S_ISREG(inode->i_mode) &&
			    ll_i2info(inode)->lli_clob == NULL) {
				CDEBUG(D_INODE,
					"%s: apply lsm %p to inode "DFID".\n",
					ll_get_fsname(sb, NULL, 0), md->lsm,
					PFID(ll_inode2fid(inode)));
				rc = cl_file_inode_init(inode, md);
			}
			if (rc != 0) {
				make_bad_inode(inode);
				unlock_new_inode(inode);
				iput(inode);
				inode = ERR_PTR(rc);
			} else
				unlock_new_inode(inode);
		} else if (!(inode->i_state & (I_FREEING | I_CLEAR)))
			ll_update_inode(inode, md);
		CDEBUG(D_VFSTRACE, "got inode: %p for "DFID"\n",
		       inode, PFID(&md->body->fid1));
	}
	return inode;
}

static void ll_invalidate_negative_children(struct inode *dir)
{
	struct dentry *dentry, *tmp_subdir;
	struct ll_d_hlist_node *p;

	ll_lock_dcache(dir);
	ll_d_hlist_for_each_entry(dentry, p, &dir->i_dentry, d_u.d_alias) {
		spin_lock(&dentry->d_lock);
		if (!list_empty(&dentry->d_subdirs)) {
			struct dentry *child;

			list_for_each_entry_safe(child, tmp_subdir,
						 &dentry->d_subdirs,
						 d_child) {
				if (child->d_inode == NULL)
					d_lustre_invalidate(child, 1);
			}
		}
		spin_unlock(&dentry->d_lock);
	}
	ll_unlock_dcache(dir);
}

int ll_md_blocking_ast(struct ldlm_lock *lock, struct ldlm_lock_desc *desc,
		       void *data, int flag)
{
	struct lustre_handle lockh;
	int rc;

	switch (flag) {
	case LDLM_CB_BLOCKING:
		ldlm_lock2handle(lock, &lockh);
		rc = ldlm_cli_cancel(&lockh, LCF_ASYNC);
		if (rc < 0) {
			CDEBUG(D_INODE, "ldlm_cli_cancel: rc = %d\n", rc);
			return rc;
		}
		break;
	case LDLM_CB_CANCELING: {
		struct inode *inode = ll_inode_from_resource_lock(lock);
		__u64 bits = lock->l_policy_data.l_inodebits.bits;

		/* Inode is set to lock->l_resource->lr_lvb_inode
		 * for mdc - bug 24555 */
		LASSERT(lock->l_ast_data == NULL);

		if (inode == NULL)
			break;

		/* Invalidate all dentries associated with this inode */
		LASSERT(lock->l_flags & LDLM_FL_CANCELING);

		if (!fid_res_name_eq(ll_inode2fid(inode),
				     &lock->l_resource->lr_name)) {
			LDLM_ERROR(lock, "data mismatch with object "DFID"(%p)",
				   PFID(ll_inode2fid(inode)), inode);
			LBUG();
		}

		if (bits & MDS_INODELOCK_XATTR) {
			ll_xattr_cache_destroy(inode);
			bits &= ~MDS_INODELOCK_XATTR;
		}

		/* For OPEN locks we differentiate between lock modes
		 * LCK_CR, LCK_CW, LCK_PR - bug 22891 */
		if (bits & MDS_INODELOCK_OPEN)
			ll_have_md_lock(inode, &bits, lock->l_req_mode);

		if (bits & MDS_INODELOCK_OPEN) {
			fmode_t fmode;

			switch (lock->l_req_mode) {
			case LCK_CW:
				fmode = FMODE_WRITE;
				break;
			case LCK_PR:
				fmode = FMODE_EXEC;
				break;
			case LCK_CR:
				fmode = FMODE_READ;
				break;
			default:
				LDLM_ERROR(lock, "bad lock mode for OPEN lock");
				LBUG();
			}

			ll_md_real_close(inode, fmode);
		}

		if (bits & (MDS_INODELOCK_LOOKUP | MDS_INODELOCK_UPDATE |
			    MDS_INODELOCK_LAYOUT | MDS_INODELOCK_PERM))
			ll_have_md_lock(inode, &bits, LCK_MINMODE);

		if (bits & MDS_INODELOCK_LAYOUT) {
			struct cl_object_conf conf = {
				.coc_opc = OBJECT_CONF_INVALIDATE,
				.coc_inode = inode,
			};

			rc = ll_layout_conf(inode, &conf);
			if (rc < 0)
				CDEBUG(D_INODE, "cannot invalidate layout of "
				       DFID": rc = %d\n",
				       PFID(ll_inode2fid(inode)), rc);
		}

		if (bits & MDS_INODELOCK_UPDATE) {
			struct ll_inode_info *lli = ll_i2info(inode);

			spin_lock(&lli->lli_lock);
			lli->lli_flags &= ~LLIF_MDS_SIZE_LOCK;
			spin_unlock(&lli->lli_lock);
		}

		if ((bits & MDS_INODELOCK_UPDATE) && S_ISDIR(inode->i_mode)) {
			CDEBUG(D_INODE, "invalidating inode %lu\n",
			       inode->i_ino);
			truncate_inode_pages(inode->i_mapping, 0);
			ll_invalidate_negative_children(inode);
		}

		if ((bits & (MDS_INODELOCK_LOOKUP | MDS_INODELOCK_PERM)) &&
		    inode->i_sb->s_root != NULL &&
		    inode != inode->i_sb->s_root->d_inode)
			ll_invalidate_aliases(inode);

		iput(inode);
		break;
	}
	default:
		LBUG();
	}

	return 0;
}

__u32 ll_i2suppgid(struct inode *i)
{
	if (in_group_p(i->i_gid))
		return (__u32)from_kgid(&init_user_ns, i->i_gid);
	else
		return (__u32)(-1);
}

/* Pack the required supplementary groups into the supplied groups array.
 * If we don't need to use the groups from the target inode(s) then we
 * instead pack one or more groups from the user's supplementary group
 * array in case it might be useful.  Not needed if doing an MDS-side upcall. */
void ll_i2gids(__u32 *suppgids, struct inode *i1, struct inode *i2)
{
#if 0
	int i;
#endif

	LASSERT(i1 != NULL);
	LASSERT(suppgids != NULL);

	suppgids[0] = ll_i2suppgid(i1);

	if (i2)
		suppgids[1] = ll_i2suppgid(i2);
		else
			suppgids[1] = -1;

#if 0
	for (i = 0; i < current_ngroups; i++) {
		if (suppgids[0] == -1) {
			if (current_groups[i] != suppgids[1])
				suppgids[0] = current_groups[i];
			continue;
		}
		if (suppgids[1] == -1) {
			if (current_groups[i] != suppgids[0])
				suppgids[1] = current_groups[i];
			continue;
		}
		break;
	}
#endif
}

/*
 * try to reuse three types of dentry:
 * 1. unhashed alias, this one is unhashed by d_invalidate (but it may be valid
 *    by concurrent .revalidate).
 * 2. INVALID alias (common case for no valid ldlm lock held, but this flag may
 *    be cleared by others calling d_lustre_revalidate).
 * 3. DISCONNECTED alias.
 */
static struct dentry *ll_find_alias(struct inode *inode, struct dentry *dentry)
{
	struct dentry *alias, *discon_alias, *invalid_alias;
	struct ll_d_hlist_node *p;

	if (ll_d_hlist_empty(&inode->i_dentry))
		return NULL;

	discon_alias = invalid_alias = NULL;

	ll_lock_dcache(inode);
	ll_d_hlist_for_each_entry(alias, p, &inode->i_dentry, d_u.d_alias) {
		LASSERT(alias != dentry);

		spin_lock(&alias->d_lock);
		if (alias->d_flags & DCACHE_DISCONNECTED)
			/* LASSERT(last_discon == NULL); LU-405, bz 20055 */
			discon_alias = alias;
		else if (alias->d_parent == dentry->d_parent	     &&
			 alias->d_name.hash == dentry->d_name.hash       &&
			 alias->d_name.len == dentry->d_name.len	 &&
			 memcmp(alias->d_name.name, dentry->d_name.name,
				dentry->d_name.len) == 0)
			invalid_alias = alias;
		spin_unlock(&alias->d_lock);

		if (invalid_alias)
			break;
	}
	alias = invalid_alias ?: discon_alias ?: NULL;
	if (alias) {
		spin_lock(&alias->d_lock);
		dget_dlock(alias);
		spin_unlock(&alias->d_lock);
	}
	ll_unlock_dcache(inode);

	return alias;
}

/*
 * Similar to d_splice_alias(), but lustre treats invalid alias
 * similar to DCACHE_DISCONNECTED, and tries to use it anyway.
 */
struct dentry *ll_splice_alias(struct inode *inode, struct dentry *de)
{
	struct dentry *new;
	int rc;

	if (inode) {
		new = ll_find_alias(inode, de);
		if (new) {
			rc = ll_d_init(new);
			if (rc < 0) {
				dput(new);
				return ERR_PTR(rc);
			}
			d_move(new, de);
			iput(inode);
			CDEBUG(D_DENTRY,
			       "Reuse dentry %p inode %p refc %d flags %#x\n",
			      new, new->d_inode, d_count(new), new->d_flags);
			return new;
		}
	}
	rc = ll_d_init(de);
	if (rc < 0)
		return ERR_PTR(rc);
	d_add(de, inode);
	CDEBUG(D_DENTRY, "Add dentry %p inode %p refc %d flags %#x\n",
	       de, de->d_inode, d_count(de), de->d_flags);
	return de;
}

static int ll_lookup_it_finish(struct ptlrpc_request *request,
			       struct lookup_intent *it,
			       struct inode *parent, struct dentry **de)
{
	struct inode *inode = NULL;
	__u64 bits = 0;
	int rc;

	/* NB 1 request reference will be taken away by ll_intent_lock()
	 * when I return */
	CDEBUG(D_DENTRY, "it %p it_disposition %x\n", it,
	       it->d.lustre.it_disposition);
	if (!it_disposition(it, DISP_LOOKUP_NEG)) {
		rc = ll_prep_inode(&inode, request, (*de)->d_sb, it);
		if (rc)
			return rc;

		ll_set_lock_data(ll_i2sbi(parent)->ll_md_exp, inode, it, &bits);

		/* We used to query real size from OSTs here, but actually
		   this is not needed. For stat() calls size would be updated
		   from subsequent do_revalidate()->ll_inode_revalidate_it() in
		   2.4 and
		   vfs_getattr_it->ll_getattr()->ll_inode_revalidate_it() in 2.6
		   Everybody else who needs correct file size would call
		   ll_glimpse_size or some equivalent themselves anyway.
		   Also see bug 7198. */
	}

	/* Only hash *de if it is unhashed (new dentry).
	 * Atoimc_open may passing hashed dentries for open.
	 */
	if (d_unhashed(*de)) {
		struct dentry *alias;

		alias = ll_splice_alias(inode, *de);
		if (IS_ERR(alias))
			return PTR_ERR(alias);
		*de = alias;
	} else if (!it_disposition(it, DISP_LOOKUP_NEG)  &&
		   !it_disposition(it, DISP_OPEN_CREATE)) {
		/* With DISP_OPEN_CREATE dentry will
		   instantiated in ll_create_it. */
		LASSERT((*de)->d_inode == NULL);
		d_instantiate(*de, inode);
	}

	if (!it_disposition(it, DISP_LOOKUP_NEG)) {
		/* we have lookup look - unhide dentry */
		if (bits & MDS_INODELOCK_LOOKUP)
			d_lustre_revalidate(*de);
	} else if (!it_disposition(it, DISP_OPEN_CREATE)) {
		/* If file created on server, don't depend on parent UPDATE
		 * lock to unhide it. It is left hidden and next lookup can
		 * find it in ll_splice_alias.
		 */
		/* Check that parent has UPDATE lock. */
		struct lookup_intent parent_it = {
					.it_op = IT_GETATTR,
					.d.lustre.it_lock_handle = 0 };

		if (md_revalidate_lock(ll_i2mdexp(parent), &parent_it,
				       &ll_i2info(parent)->lli_fid, NULL)) {
			d_lustre_revalidate(*de);
			ll_intent_release(&parent_it);
		}
	}

	return 0;
}

static struct dentry *ll_lookup_it(struct inode *parent, struct dentry *dentry,
				   struct lookup_intent *it, int lookup_flags)
{
	struct lookup_intent lookup_it = { .it_op = IT_LOOKUP };
	struct dentry *save = dentry, *retval;
	struct ptlrpc_request *req = NULL;
	struct md_op_data *op_data;
	__u32 opc;
	int rc;

	if (dentry->d_name.len > ll_i2sbi(parent)->ll_namelen)
		return ERR_PTR(-ENAMETOOLONG);

	CDEBUG(D_VFSTRACE, "VFS Op:name=%.*s,dir=%lu/%u(%p),intent=%s\n",
	       dentry->d_name.len, dentry->d_name.name, parent->i_ino,
	       parent->i_generation, parent, LL_IT2STR(it));

	if (d_mountpoint(dentry))
		CERROR("Tell Peter, lookup on mtpt, it %s\n", LL_IT2STR(it));

	if (it == NULL || it->it_op == IT_GETXATTR)
		it = &lookup_it;

	if (it->it_op == IT_GETATTR) {
		rc = ll_statahead_enter(parent, &dentry, 0);
		if (rc == 1) {
			if (dentry == save)
				retval = NULL;
			else
				retval = dentry;
			goto out;
		}
	}

	if (it->it_op & IT_CREAT)
		opc = LUSTRE_OPC_CREATE;
	else
		opc = LUSTRE_OPC_ANY;

	op_data = ll_prep_md_op_data(NULL, parent, NULL, dentry->d_name.name,
				     dentry->d_name.len, lookup_flags, opc,
				     NULL);
	if (IS_ERR(op_data))
		return (void *)op_data;

	/* enforce umask if acl disabled or MDS doesn't support umask */
	if (!IS_POSIXACL(parent) || !exp_connect_umask(ll_i2mdexp(parent)))
		it->it_create_mode &= ~current_umask();

	rc = md_intent_lock(ll_i2mdexp(parent), op_data, NULL, 0, it,
			    lookup_flags, &req, ll_md_blocking_ast, 0);
	ll_finish_md_op_data(op_data);
	if (rc < 0) {
		retval = ERR_PTR(rc);
		goto out;
	}

	rc = ll_lookup_it_finish(req, it, parent, &dentry);
	if (rc != 0) {
		ll_intent_release(it);
		retval = ERR_PTR(rc);
		goto out;
	}

	if ((it->it_op & IT_OPEN) && dentry->d_inode &&
	    !S_ISREG(dentry->d_inode->i_mode) &&
	    !S_ISDIR(dentry->d_inode->i_mode)) {
		ll_release_openhandle(dentry, it);
	}
	ll_lookup_finish_locks(it, dentry);

	if (dentry == save)
		retval = NULL;
	else
		retval = dentry;
	goto out;
 out:
	if (req)
		ptlrpc_req_finished(req);
	if (it->it_op == IT_GETATTR && (retval == NULL || retval == dentry))
		ll_statahead_mark(parent, dentry);
	return retval;
}

static struct dentry *ll_lookup_nd(struct inode *parent, struct dentry *dentry,
				   unsigned int flags)
{
	struct lookup_intent *itp, it = { .it_op = IT_GETATTR };
	struct dentry *de;

	CDEBUG(D_VFSTRACE, "VFS Op:name=%.*s,dir=%lu/%u(%p),flags=%u\n",
	       dentry->d_name.len, dentry->d_name.name, parent->i_ino,
	       parent->i_generation, parent, flags);

	/* Optimize away (CREATE && !OPEN). Let .create handle the race. */
	if ((flags & LOOKUP_CREATE) && !(flags & LOOKUP_OPEN))
		return NULL;

	if (flags & (LOOKUP_PARENT|LOOKUP_OPEN|LOOKUP_CREATE))
		itp = NULL;
	else
		itp = &it;
	de = ll_lookup_it(parent, dentry, itp, 0);

	if (itp != NULL)
		ll_intent_release(itp);

	return de;
}

/*
 * For cached negative dentry and new dentry, handle lookup/create/open
 * together.
 */
static int ll_atomic_open(struct inode *dir, struct dentry *dentry,
			  struct file *file, unsigned open_flags,
			  umode_t mode, int *opened)
{
	struct lookup_intent *it;
	struct dentry *de;
	long long lookup_flags = LOOKUP_OPEN;
	int rc = 0;

	CDEBUG(D_VFSTRACE, "VFS Op:name=%.*s,dir=%lu/%u(%p),file %p,"
			   "open_flags %x,mode %x opened %d\n",
	       dentry->d_name.len, dentry->d_name.name, dir->i_ino,
	       dir->i_generation, dir, file, open_flags, mode, *opened);

	it = kzalloc(sizeof(*it), GFP_NOFS);
	if (!it)
		return -ENOMEM;

	it->it_op = IT_OPEN;
	if (open_flags & O_CREAT) {
		it->it_op |= IT_CREAT;
		lookup_flags |= LOOKUP_CREATE;
	}
	it->it_create_mode = (mode & S_IALLUGO) | S_IFREG;
	it->it_flags = (open_flags & ~O_ACCMODE) | OPEN_FMODE(open_flags);

	/* Dentry added to dcache tree in ll_lookup_it */
	de = ll_lookup_it(dir, dentry, it, lookup_flags);
	if (IS_ERR(de))
		rc = PTR_ERR(de);
	else if (de != NULL)
		dentry = de;

	if (!rc) {
		if (it_disposition(it, DISP_OPEN_CREATE)) {
			/* Dentry instantiated in ll_create_it. */
			rc = ll_create_it(dir, dentry, mode, it);
			if (rc) {
				/* We dget in ll_splice_alias. */
				if (de != NULL)
					dput(de);
				goto out_release;
			}

			*opened |= FILE_CREATED;
		}
		if (dentry->d_inode && it_disposition(it, DISP_OPEN_OPEN)) {
			/* Open dentry. */
			if (S_ISFIFO(dentry->d_inode->i_mode)) {
				/* We cannot call open here as it would
				 * deadlock.
				 */
				if (it_disposition(it, DISP_ENQ_OPEN_REF))
					ptlrpc_req_finished(
						       (struct ptlrpc_request *)
							  it->d.lustre.it_data);
				rc = finish_no_open(file, de);
			} else {
				file->private_data = it;
				rc = finish_open(file, dentry, NULL, opened);
				/* We dget in ll_splice_alias. finish_open takes
				 * care of dget for fd open.
				 */
				if (de != NULL)
					dput(de);
			}
		} else {
			rc = finish_no_open(file, de);
		}
	}

out_release:
	ll_intent_release(it);
	OBD_FREE(it, sizeof(*it));

	return rc;
}


/* We depend on "mode" being set with the proper file type/umask by now */
static struct inode *ll_create_node(struct inode *dir, struct lookup_intent *it)
{
	struct inode *inode = NULL;
	struct ptlrpc_request *request = NULL;
	struct ll_sb_info *sbi = ll_i2sbi(dir);
	int rc;

	LASSERT(it && it->d.lustre.it_disposition);

	LASSERT(it_disposition(it, DISP_ENQ_CREATE_REF));
	request = it->d.lustre.it_data;
	it_clear_disposition(it, DISP_ENQ_CREATE_REF);
	rc = ll_prep_inode(&inode, request, dir->i_sb, it);
	if (rc) {
		inode = ERR_PTR(rc);
		goto out;
	}

	LASSERT(ll_d_hlist_empty(&inode->i_dentry));

	/* We asked for a lock on the directory, but were granted a
	 * lock on the inode.  Since we finally have an inode pointer,
	 * stuff it in the lock. */
	CDEBUG(D_DLMTRACE, "setting l_ast_data to inode %p (%lu/%u)\n",
	       inode, inode->i_ino, inode->i_generation);
	ll_set_lock_data(sbi->ll_md_exp, inode, it, NULL);
 out:
	ptlrpc_req_finished(request);
	return inode;
}

/*
 * By the time this is called, we already have created the directory cache
 * entry for the new file, but it is so far negative - it has no inode.
 *
 * We defer creating the OBD object(s) until open, to keep the intent and
 * non-intent code paths similar, and also because we do not have the MDS
 * inode number before calling ll_create_node() (which is needed for LOV),
 * so we would need to do yet another RPC to the MDS to store the LOV EA
 * data on the MDS.  If needed, we would pass the PACKED lmm as data and
 * lmm_size in datalen (the MDS still has code which will handle that).
 *
 * If the create succeeds, we fill in the inode information
 * with d_instantiate().
 */
static int ll_create_it(struct inode *dir, struct dentry *dentry, int mode,
			struct lookup_intent *it)
{
	struct inode *inode;
	int rc = 0;

	CDEBUG(D_VFSTRACE, "VFS Op:name=%.*s,dir=%lu/%u(%p),intent=%s\n",
	       dentry->d_name.len, dentry->d_name.name, dir->i_ino,
	       dir->i_generation, dir, LL_IT2STR(it));

	rc = it_open_error(DISP_OPEN_CREATE, it);
	if (rc)
		return rc;

	inode = ll_create_node(dir, it);
	if (IS_ERR(inode))
		return PTR_ERR(inode);

	d_instantiate(dentry, inode);
	return 0;
}

static void ll_update_times(struct ptlrpc_request *request,
			    struct inode *inode)
{
	struct mdt_body *body = req_capsule_server_get(&request->rq_pill,
						       &RMF_MDT_BODY);

	LASSERT(body);
	if (body->valid & OBD_MD_FLMTIME &&
	    body->mtime > LTIME_S(inode->i_mtime)) {
		CDEBUG(D_INODE, "setting ino %lu mtime from %lu to %llu\n",
		       inode->i_ino, LTIME_S(inode->i_mtime), body->mtime);
		LTIME_S(inode->i_mtime) = body->mtime;
	}
	if (body->valid & OBD_MD_FLCTIME &&
	    body->ctime > LTIME_S(inode->i_ctime))
		LTIME_S(inode->i_ctime) = body->ctime;
}

static int ll_new_node(struct inode *dir, struct qstr *name,
		       const char *tgt, int mode, int rdev,
		       struct dentry *dchild, __u32 opc)
{
	struct ptlrpc_request *request = NULL;
	struct md_op_data *op_data;
	struct inode *inode = NULL;
	struct ll_sb_info *sbi = ll_i2sbi(dir);
	int tgt_len = 0;
	int err;

	if (unlikely(tgt != NULL))
		tgt_len = strlen(tgt) + 1;

	op_data = ll_prep_md_op_data(NULL, dir, NULL, name->name,
				     name->len, 0, opc, NULL);
	if (IS_ERR(op_data)) {
		err = PTR_ERR(op_data);
		goto err_exit;
	}

	err = md_create(sbi->ll_md_exp, op_data, tgt, tgt_len, mode,
			from_kuid(&init_user_ns, current_fsuid()),
			from_kgid(&init_user_ns, current_fsgid()),
			cfs_curproc_cap_pack(), rdev, &request);
	ll_finish_md_op_data(op_data);
	if (err)
		goto err_exit;

	ll_update_times(request, dir);

	if (dchild) {
		err = ll_prep_inode(&inode, request, dchild->d_sb, NULL);
		if (err)
			goto err_exit;

		d_instantiate(dchild, inode);
	}
err_exit:
	ptlrpc_req_finished(request);

	return err;
}

static int ll_mknod_generic(struct inode *dir, struct qstr *name, int mode,
			    unsigned rdev, struct dentry *dchild)
{
	int err;

	CDEBUG(D_VFSTRACE, "VFS Op:name=%.*s,dir=%lu/%u(%p) mode %o dev %x\n",
	       name->len, name->name, dir->i_ino, dir->i_generation, dir,
	       mode, rdev);

	if (!IS_POSIXACL(dir) || !exp_connect_umask(ll_i2mdexp(dir)))
		mode &= ~current_umask();

	switch (mode & S_IFMT) {
	case 0:
		mode |= S_IFREG; /* for mode = 0 case, fallthrough */
	case S_IFREG:
	case S_IFCHR:
	case S_IFBLK:
	case S_IFIFO:
	case S_IFSOCK:
		err = ll_new_node(dir, name, NULL, mode, rdev, dchild,
				  LUSTRE_OPC_MKNOD);
		break;
	case S_IFDIR:
		err = -EPERM;
		break;
	default:
		err = -EINVAL;
	}

	if (!err)
		ll_stats_ops_tally(ll_i2sbi(dir), LPROC_LL_MKNOD, 1);

	return err;
}

/*
 * Plain create. Intent create is handled in atomic_open.
 */
static int ll_create_nd(struct inode *dir, struct dentry *dentry,
			umode_t mode, bool want_excl)
{
	int rc;

	CDEBUG(D_VFSTRACE, "VFS Op:name=%.*s,dir=%lu/%u(%p),"
			   "flags=%u, excl=%d\n",
	       dentry->d_name.len, dentry->d_name.name, dir->i_ino,
	       dir->i_generation, dir, mode, want_excl);

	rc = ll_mknod_generic(dir, &dentry->d_name, mode, 0, dentry);

	ll_stats_ops_tally(ll_i2sbi(dir), LPROC_LL_CREATE, 1);

	CDEBUG(D_VFSTRACE, "VFS Op:name=%.*s, unhashed %d\n",
	       dentry->d_name.len, dentry->d_name.name, d_unhashed(dentry));

	return rc;
}

static int ll_symlink_generic(struct inode *dir, struct qstr *name,
			      const char *tgt, struct dentry *dchild)
{
	int err;

	CDEBUG(D_VFSTRACE, "VFS Op:name=%.*s,dir=%lu/%u(%p),target=%.*s\n",
	       name->len, name->name, dir->i_ino, dir->i_generation,
	       dir, 3000, tgt);

	err = ll_new_node(dir, name, (char *)tgt, S_IFLNK | S_IRWXUGO,
			  0, dchild, LUSTRE_OPC_SYMLINK);

	if (!err)
		ll_stats_ops_tally(ll_i2sbi(dir), LPROC_LL_SYMLINK, 1);

	return err;
}

static int ll_link_generic(struct inode *src,  struct inode *dir,
			   struct qstr *name, struct dentry *dchild)
{
	struct ll_sb_info *sbi = ll_i2sbi(dir);
	struct ptlrpc_request *request = NULL;
	struct md_op_data *op_data;
	int err;

	CDEBUG(D_VFSTRACE,
	       "VFS Op: inode=%lu/%u(%p), dir=%lu/%u(%p), target=%.*s\n",
	       src->i_ino, src->i_generation, src, dir->i_ino,
	       dir->i_generation, dir, name->len, name->name);

	op_data = ll_prep_md_op_data(NULL, src, dir, name->name, name->len,
				     0, LUSTRE_OPC_ANY, NULL);
	if (IS_ERR(op_data))
		return PTR_ERR(op_data);

	err = md_link(sbi->ll_md_exp, op_data, &request);
	ll_finish_md_op_data(op_data);
	if (err)
		goto out;

	ll_update_times(request, dir);
	ll_stats_ops_tally(sbi, LPROC_LL_LINK, 1);
out:
	ptlrpc_req_finished(request);
	return err;
}

static int ll_mkdir_generic(struct inode *dir, struct qstr *name,
			    int mode, struct dentry *dchild)

{
	int err;

	CDEBUG(D_VFSTRACE, "VFS Op:name=%.*s,dir=%lu/%u(%p)\n",
	       name->len, name->name, dir->i_ino, dir->i_generation, dir);

	if (!IS_POSIXACL(dir) || !exp_connect_umask(ll_i2mdexp(dir)))
		mode &= ~current_umask();
	mode = (mode & (S_IRWXUGO|S_ISVTX)) | S_IFDIR;
	err = ll_new_node(dir, name, NULL, mode, 0, dchild, LUSTRE_OPC_MKDIR);

	if (!err)
		ll_stats_ops_tally(ll_i2sbi(dir), LPROC_LL_MKDIR, 1);

	return err;
}

/* Try to find the child dentry by its name.
   If found, put the result fid into @fid. */
static void ll_get_child_fid(struct inode * dir, struct qstr *name,
			     struct lu_fid *fid)
{
	struct dentry *parent, *child;

	parent = ll_d_hlist_entry(dir->i_dentry, struct dentry, d_u.d_alias);
	child = d_lookup(parent, name);
	if (child) {
		if (child->d_inode)
			*fid = *ll_inode2fid(child->d_inode);
		dput(child);
	}
}

static int ll_rmdir_generic(struct inode *dir, struct dentry *dparent,
			    struct dentry *dchild, struct qstr *name)
{
	struct ptlrpc_request *request = NULL;
	struct md_op_data *op_data;
	int rc;

	CDEBUG(D_VFSTRACE, "VFS Op:name=%.*s,dir=%lu/%u(%p)\n",
	       name->len, name->name, dir->i_ino, dir->i_generation, dir);

	if (unlikely(ll_d_mountpoint(dparent, dchild, name)))
		return -EBUSY;

	op_data = ll_prep_md_op_data(NULL, dir, NULL, name->name, name->len,
				     S_IFDIR, LUSTRE_OPC_ANY, NULL);
	if (IS_ERR(op_data))
		return PTR_ERR(op_data);

	ll_get_child_fid(dir, name, &op_data->op_fid3);
	op_data->op_fid2 = op_data->op_fid3;
	rc = md_unlink(ll_i2sbi(dir)->ll_md_exp, op_data, &request);
	ll_finish_md_op_data(op_data);
	if (rc == 0) {
		ll_update_times(request, dir);
		ll_stats_ops_tally(ll_i2sbi(dir), LPROC_LL_RMDIR, 1);
	}

	ptlrpc_req_finished(request);
	return rc;
}

/**
 * Remove dir entry
 **/
int ll_rmdir_entry(struct inode *dir, char *name, int namelen)
{
	struct ptlrpc_request *request = NULL;
	struct md_op_data *op_data;
	int rc;

	CDEBUG(D_VFSTRACE, "VFS Op:name=%.*s,dir=%lu/%u(%p)\n",
	       namelen, name, dir->i_ino, dir->i_generation, dir);

	op_data = ll_prep_md_op_data(NULL, dir, NULL, name, strlen(name),
				     S_IFDIR, LUSTRE_OPC_ANY, NULL);
	if (IS_ERR(op_data))
		return PTR_ERR(op_data);
	op_data->op_cli_flags |= CLI_RM_ENTRY;
	rc = md_unlink(ll_i2sbi(dir)->ll_md_exp, op_data, &request);
	ll_finish_md_op_data(op_data);
	if (rc == 0) {
		ll_update_times(request, dir);
		ll_stats_ops_tally(ll_i2sbi(dir), LPROC_LL_RMDIR, 1);
	}

	ptlrpc_req_finished(request);
	return rc;
}

int ll_objects_destroy(struct ptlrpc_request *request, struct inode *dir)
{
	struct mdt_body *body;
	struct lov_mds_md *eadata;
	struct lov_stripe_md *lsm = NULL;
	struct obd_trans_info oti = { 0 };
	struct obdo *oa;
	struct obd_capa *oc = NULL;
	int rc;

	/* req is swabbed so this is safe */
	body = req_capsule_server_get(&request->rq_pill, &RMF_MDT_BODY);
	if (!(body->valid & OBD_MD_FLEASIZE))
		return 0;

	if (body->eadatasize == 0) {
		CERROR("OBD_MD_FLEASIZE set but eadatasize zero\n");
		rc = -EPROTO;
		goto out;
	}

	/* The MDS sent back the EA because we unlinked the last reference
	 * to this file. Use this EA to unlink the objects on the OST.
	 * It's opaque so we don't swab here; we leave it to obd_unpackmd() to
	 * check it is complete and sensible. */
	eadata = req_capsule_server_sized_get(&request->rq_pill, &RMF_MDT_MD,
					      body->eadatasize);
	LASSERT(eadata != NULL);

	rc = obd_unpackmd(ll_i2dtexp(dir), &lsm, eadata, body->eadatasize);
	if (rc < 0) {
		CERROR("obd_unpackmd: %d\n", rc);
		goto out;
	}
	LASSERT(rc >= sizeof(*lsm));

	OBDO_ALLOC(oa);
	if (oa == NULL) {
		rc = -ENOMEM;
		goto out_free_memmd;
	}

	oa->o_oi = lsm->lsm_oi;
	oa->o_mode = body->mode & S_IFMT;
	oa->o_valid = OBD_MD_FLID | OBD_MD_FLTYPE | OBD_MD_FLGROUP;

	if (body->valid & OBD_MD_FLCOOKIE) {
		oa->o_valid |= OBD_MD_FLCOOKIE;
		oti.oti_logcookies =
			req_capsule_server_sized_get(&request->rq_pill,
						     &RMF_LOGCOOKIES,
						   sizeof(struct llog_cookie) *
						     lsm->lsm_stripe_count);
		if (oti.oti_logcookies == NULL) {
			oa->o_valid &= ~OBD_MD_FLCOOKIE;
			body->valid &= ~OBD_MD_FLCOOKIE;
		}
	}

	if (body->valid & OBD_MD_FLOSSCAPA) {
		rc = md_unpack_capa(ll_i2mdexp(dir), request, &RMF_CAPA2, &oc);
		if (rc)
			goto out_free_memmd;
	}

	rc = obd_destroy(NULL, ll_i2dtexp(dir), oa, lsm, &oti,
			 ll_i2mdexp(dir), oc);
	capa_put(oc);
	if (rc)
		CERROR("obd destroy objid "DOSTID" error %d\n",
		       POSTID(&lsm->lsm_oi), rc);
out_free_memmd:
	obd_free_memmd(ll_i2dtexp(dir), &lsm);
	OBDO_FREE(oa);
out:
	return rc;
}

/* ll_unlink_generic() doesn't update the inode with the new link count.
 * Instead, ll_ddelete() and ll_d_iput() will update it based upon if there
 * is any lock existing. They will recycle dentries and inodes based upon locks
 * too. b=20433 */
static int ll_unlink_generic(struct inode *dir, struct dentry *dparent,
			     struct dentry *dchild, struct qstr *name)
{
	struct ptlrpc_request *request = NULL;
	struct md_op_data *op_data;
	int rc;
	CDEBUG(D_VFSTRACE, "VFS Op:name=%.*s,dir=%lu/%u(%p)\n",
	       name->len, name->name, dir->i_ino, dir->i_generation, dir);

	/*
	 * XXX: unlink bind mountpoint maybe call to here,
	 * just check it as vfs_unlink does.
	 */
	if (unlikely(ll_d_mountpoint(dparent, dchild, name)))
		return -EBUSY;

	op_data = ll_prep_md_op_data(NULL, dir, NULL, name->name,
				     name->len, 0, LUSTRE_OPC_ANY, NULL);
	if (IS_ERR(op_data))
		return PTR_ERR(op_data);

	ll_get_child_fid(dir, name, &op_data->op_fid3);
	op_data->op_fid2 = op_data->op_fid3;
	rc = md_unlink(ll_i2sbi(dir)->ll_md_exp, op_data, &request);
	ll_finish_md_op_data(op_data);
	if (rc)
		goto out;

	ll_update_times(request, dir);
	ll_stats_ops_tally(ll_i2sbi(dir), LPROC_LL_UNLINK, 1);

	rc = ll_objects_destroy(request, dir);
 out:
	ptlrpc_req_finished(request);
	return rc;
}

static int ll_rename_generic(struct inode *src, struct dentry *src_dparent,
			     struct dentry *src_dchild, struct qstr *src_name,
			     struct inode *tgt, struct dentry *tgt_dparent,
			     struct dentry *tgt_dchild, struct qstr *tgt_name)
{
	struct ptlrpc_request *request = NULL;
	struct ll_sb_info *sbi = ll_i2sbi(src);
	struct md_op_data *op_data;
	int err;

	CDEBUG(D_VFSTRACE,
	       "VFS Op:oldname=%.*s,src_dir=%lu/%u(%p),newname=%.*s,"
	       "tgt_dir=%lu/%u(%p)\n", src_name->len, src_name->name,
	       src->i_ino, src->i_generation, src, tgt_name->len,
	       tgt_name->name, tgt->i_ino, tgt->i_generation, tgt);

	if (unlikely(ll_d_mountpoint(src_dparent, src_dchild, src_name) ||
	    ll_d_mountpoint(tgt_dparent, tgt_dchild, tgt_name)))
		return -EBUSY;

	op_data = ll_prep_md_op_data(NULL, src, tgt, NULL, 0, 0,
				     LUSTRE_OPC_ANY, NULL);
	if (IS_ERR(op_data))
		return PTR_ERR(op_data);

	ll_get_child_fid(src, src_name, &op_data->op_fid3);
	ll_get_child_fid(tgt, tgt_name, &op_data->op_fid4);
	err = md_rename(sbi->ll_md_exp, op_data,
			src_name->name, src_name->len,
			tgt_name->name, tgt_name->len, &request);
	ll_finish_md_op_data(op_data);
	if (!err) {
		ll_update_times(request, src);
		ll_update_times(request, tgt);
		ll_stats_ops_tally(sbi, LPROC_LL_RENAME, 1);
		err = ll_objects_destroy(request, src);
	}

	ptlrpc_req_finished(request);

	return err;
}

static int ll_mknod(struct inode *dir, struct dentry *dchild, ll_umode_t mode,
		    dev_t rdev)
{
	return ll_mknod_generic(dir, &dchild->d_name, mode,
				old_encode_dev(rdev), dchild);
}

static int ll_unlink(struct inode * dir, struct dentry *dentry)
{
	return ll_unlink_generic(dir, NULL, dentry, &dentry->d_name);
}

static int ll_mkdir(struct inode *dir, struct dentry *dentry, ll_umode_t mode)
{
	return ll_mkdir_generic(dir, &dentry->d_name, mode, dentry);
}

static int ll_rmdir(struct inode *dir, struct dentry *dentry)
{
	return ll_rmdir_generic(dir, NULL, dentry, &dentry->d_name);
}

static int ll_symlink(struct inode *dir, struct dentry *dentry,
		      const char *oldname)
{
	return ll_symlink_generic(dir, &dentry->d_name, oldname, dentry);
}

static int ll_link(struct dentry *old_dentry, struct inode *dir,
		   struct dentry *new_dentry)
{
	return ll_link_generic(old_dentry->d_inode, dir, &new_dentry->d_name,
			       new_dentry);
}

static int ll_rename(struct inode *old_dir, struct dentry *old_dentry,
		     struct inode *new_dir, struct dentry *new_dentry)
{
	int err;
	err = ll_rename_generic(old_dir, NULL,
				 old_dentry, &old_dentry->d_name,
				 new_dir, NULL, new_dentry,
				 &new_dentry->d_name);
	if (!err) {
			d_move(old_dentry, new_dentry);
	}
	return err;
}

const struct inode_operations ll_dir_inode_operations = {
	.mknod	      = ll_mknod,
	.atomic_open	    = ll_atomic_open,
	.lookup	     = ll_lookup_nd,
	.create	     = ll_create_nd,
	/* We need all these non-raw things for NFSD, to not patch it. */
	.unlink	     = ll_unlink,
	.mkdir	      = ll_mkdir,
	.rmdir	      = ll_rmdir,
	.symlink	    = ll_symlink,
	.link	       = ll_link,
	.rename	     = ll_rename,
	.setattr	    = ll_setattr,
	.getattr	    = ll_getattr,
	.permission	 = ll_inode_permission,
	.setxattr	   = ll_setxattr,
	.getxattr	   = ll_getxattr,
	.listxattr	  = ll_listxattr,
	.removexattr	= ll_removexattr,
	.get_acl	    = ll_get_acl,
};

const struct inode_operations ll_special_inode_operations = {
	.setattr	= ll_setattr,
	.getattr	= ll_getattr,
	.permission     = ll_inode_permission,
	.setxattr       = ll_setxattr,
	.getxattr       = ll_getxattr,
	.listxattr      = ll_listxattr,
	.removexattr    = ll_removexattr,
	.get_acl	    = ll_get_acl,
};
