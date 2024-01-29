// SPDX-License-Identifier: GPL-2.0-or-later
/* AFS silly rename handling
 *
 * Copyright (C) 2019 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 * - Derived from NFS's sillyrename.
 */

#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/namei.h>
#include <linux/fsnotify.h>
#include "internal.h"

static void afs_silly_rename_success(struct afs_operation *op)
{
	_enter("op=%08x", op->debug_id);

	afs_check_dir_conflict(op, &op->file[0]);
	afs_vnode_commit_status(op, &op->file[0]);
}

static void afs_silly_rename_edit_dir(struct afs_operation *op)
{
	struct afs_vnode_param *dvp = &op->file[0];
	struct afs_vnode *dvnode = dvp->vnode;
	struct afs_vnode *vnode = AFS_FS_I(d_inode(op->dentry));
	struct dentry *old = op->dentry;
	struct dentry *new = op->dentry_2;

	spin_lock(&old->d_lock);
	old->d_flags |= DCACHE_NFSFS_RENAMED;
	spin_unlock(&old->d_lock);
	if (dvnode->silly_key != op->key) {
		key_put(dvnode->silly_key);
		dvnode->silly_key = key_get(op->key);
	}

	down_write(&dvnode->validate_lock);
	if (test_bit(AFS_VNODE_DIR_VALID, &dvnode->flags) &&
	    dvnode->status.data_version == dvp->dv_before + dvp->dv_delta) {
		afs_edit_dir_remove(dvnode, &old->d_name,
				    afs_edit_dir_for_silly_0);
		afs_edit_dir_add(dvnode, &new->d_name,
				 &vnode->fid, afs_edit_dir_for_silly_1);
	}
	up_write(&dvnode->validate_lock);
}

static const struct afs_operation_ops afs_silly_rename_operation = {
	.issue_afs_rpc	= afs_fs_rename,
	.issue_yfs_rpc	= yfs_fs_rename,
	.success	= afs_silly_rename_success,
	.edit_dir	= afs_silly_rename_edit_dir,
};

/*
 * Actually perform the silly rename step.
 */
static int afs_do_silly_rename(struct afs_vnode *dvnode, struct afs_vnode *vnode,
			       struct dentry *old, struct dentry *new,
			       struct key *key)
{
	struct afs_operation *op;

	_enter("%pd,%pd", old, new);

	op = afs_alloc_operation(key, dvnode->volume);
	if (IS_ERR(op))
		return PTR_ERR(op);

	afs_op_set_vnode(op, 0, dvnode);
	afs_op_set_vnode(op, 1, dvnode);
	op->file[0].dv_delta = 1;
	op->file[1].dv_delta = 1;
	op->file[0].modification = true;
	op->file[1].modification = true;
	op->file[0].update_ctime = true;
	op->file[1].update_ctime = true;

	op->dentry		= old;
	op->dentry_2		= new;
	op->ops			= &afs_silly_rename_operation;

	trace_afs_silly_rename(vnode, false);
	return afs_do_sync_operation(op);
}

/*
 * Perform silly-rename of a dentry.
 *
 * AFS is stateless and the server doesn't know when the client is holding a
 * file open.  To prevent application problems when a file is unlinked while
 * it's still open, the client performs a "silly-rename".  That is, it renames
 * the file to a hidden file in the same directory, and only performs the
 * unlink once the last reference to it is put.
 *
 * The final cleanup is done during dentry_iput.
 */
int afs_sillyrename(struct afs_vnode *dvnode, struct afs_vnode *vnode,
		    struct dentry *dentry, struct key *key)
{
	static unsigned int sillycounter;
	struct dentry *sdentry = NULL;
	unsigned char silly[16];
	int ret = -EBUSY;

	_enter("");

	/* We don't allow a dentry to be silly-renamed twice. */
	if (dentry->d_flags & DCACHE_NFSFS_RENAMED)
		return -EBUSY;

	sdentry = NULL;
	do {
		int slen;

		dput(sdentry);
		sillycounter++;

		/* Create a silly name.  Note that the ".__afs" prefix is
		 * understood by the salvager and must not be changed.
		 */
		slen = scnprintf(silly, sizeof(silly), ".__afs%04X", sillycounter);
		sdentry = lookup_one_len(silly, dentry->d_parent, slen);

		/* N.B. Better to return EBUSY here ... it could be dangerous
		 * to delete the file while it's in use.
		 */
		if (IS_ERR(sdentry))
			goto out;
	} while (!d_is_negative(sdentry));

	ihold(&vnode->netfs.inode);

	ret = afs_do_silly_rename(dvnode, vnode, dentry, sdentry, key);
	switch (ret) {
	case 0:
		/* The rename succeeded. */
		set_bit(AFS_VNODE_SILLY_DELETED, &vnode->flags);
		d_move(dentry, sdentry);
		break;
	case -ERESTARTSYS:
		/* The result of the rename is unknown. Play it safe by forcing
		 * a new lookup.
		 */
		d_drop(dentry);
		d_drop(sdentry);
	}

	iput(&vnode->netfs.inode);
	dput(sdentry);
out:
	_leave(" = %d", ret);
	return ret;
}

static void afs_silly_unlink_success(struct afs_operation *op)
{
	_enter("op=%08x", op->debug_id);
	afs_check_dir_conflict(op, &op->file[0]);
	afs_vnode_commit_status(op, &op->file[0]);
	afs_vnode_commit_status(op, &op->file[1]);
	afs_update_dentry_version(op, &op->file[0], op->dentry);
}

static void afs_silly_unlink_edit_dir(struct afs_operation *op)
{
	struct afs_vnode_param *dvp = &op->file[0];
	struct afs_vnode *dvnode = dvp->vnode;

	_enter("op=%08x", op->debug_id);
	down_write(&dvnode->validate_lock);
	if (test_bit(AFS_VNODE_DIR_VALID, &dvnode->flags) &&
	    dvnode->status.data_version == dvp->dv_before + dvp->dv_delta)
		afs_edit_dir_remove(dvnode, &op->dentry->d_name,
				    afs_edit_dir_for_unlink);
	up_write(&dvnode->validate_lock);
}

static const struct afs_operation_ops afs_silly_unlink_operation = {
	.issue_afs_rpc	= afs_fs_remove_file,
	.issue_yfs_rpc	= yfs_fs_remove_file,
	.success	= afs_silly_unlink_success,
	.aborted	= afs_check_for_remote_deletion,
	.edit_dir	= afs_silly_unlink_edit_dir,
};

/*
 * Tell the server to remove a sillyrename file.
 */
static int afs_do_silly_unlink(struct afs_vnode *dvnode, struct afs_vnode *vnode,
			       struct dentry *dentry, struct key *key)
{
	struct afs_operation *op;

	_enter("");

	op = afs_alloc_operation(NULL, dvnode->volume);
	if (IS_ERR(op))
		return PTR_ERR(op);

	afs_op_set_vnode(op, 0, dvnode);
	afs_op_set_vnode(op, 1, vnode);
	op->file[0].dv_delta = 1;
	op->file[0].modification = true;
	op->file[0].update_ctime = true;
	op->file[1].op_unlinked = true;
	op->file[1].update_ctime = true;

	op->dentry	= dentry;
	op->ops		= &afs_silly_unlink_operation;

	trace_afs_silly_rename(vnode, true);
	afs_begin_vnode_operation(op);
	afs_wait_for_operation(op);

	/* If there was a conflict with a third party, check the status of the
	 * unlinked vnode.
	 */
	if (op->cumul_error.error == 0 && (op->flags & AFS_OPERATION_DIR_CONFLICT)) {
		op->file[1].update_ctime = false;
		op->fetch_status.which = 1;
		op->ops = &afs_fetch_status_operation;
		afs_begin_vnode_operation(op);
		afs_wait_for_operation(op);
	}

	return afs_put_operation(op);
}

/*
 * Remove sillyrename file on iput.
 */
int afs_silly_iput(struct dentry *dentry, struct inode *inode)
{
	struct afs_vnode *dvnode = AFS_FS_I(d_inode(dentry->d_parent));
	struct afs_vnode *vnode = AFS_FS_I(inode);
	struct dentry *alias;
	int ret;

	DECLARE_WAIT_QUEUE_HEAD_ONSTACK(wq);

	_enter("%p{%pd},%llx", dentry, dentry, vnode->fid.vnode);

	down_read(&dvnode->rmdir_lock);

	alias = d_alloc_parallel(dentry->d_parent, &dentry->d_name, &wq);
	if (IS_ERR(alias)) {
		up_read(&dvnode->rmdir_lock);
		return 0;
	}

	if (!d_in_lookup(alias)) {
		/* We raced with lookup...  See if we need to transfer the
		 * sillyrename information to the aliased dentry.
		 */
		ret = 0;
		spin_lock(&alias->d_lock);
		if (d_really_is_positive(alias) &&
		    !(alias->d_flags & DCACHE_NFSFS_RENAMED)) {
			alias->d_flags |= DCACHE_NFSFS_RENAMED;
			ret = 1;
		}
		spin_unlock(&alias->d_lock);
		up_read(&dvnode->rmdir_lock);
		dput(alias);
		return ret;
	}

	/* Stop lock-release from complaining. */
	spin_lock(&vnode->lock);
	vnode->lock_state = AFS_VNODE_LOCK_DELETED;
	trace_afs_flock_ev(vnode, NULL, afs_flock_silly_delete, 0);
	spin_unlock(&vnode->lock);

	afs_do_silly_unlink(dvnode, vnode, dentry, dvnode->silly_key);
	up_read(&dvnode->rmdir_lock);
	d_lookup_done(alias);
	dput(alias);
	return 1;
}
