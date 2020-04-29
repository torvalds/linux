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

/*
 * Actually perform the silly rename step.
 */
static int afs_do_silly_rename(struct afs_vnode *dvnode, struct afs_vnode *vnode,
			       struct dentry *old, struct dentry *new,
			       struct key *key)
{
	struct afs_fs_cursor fc;
	struct afs_status_cb *scb;
	afs_dataversion_t dir_data_version;
	int ret = -ERESTARTSYS;

	_enter("%pd,%pd", old, new);

	scb = kzalloc(sizeof(struct afs_status_cb), GFP_KERNEL);
	if (!scb)
		return -ENOMEM;

	trace_afs_silly_rename(vnode, false);
	if (afs_begin_vnode_operation(&fc, dvnode, key, true)) {
		dir_data_version = dvnode->status.data_version + 1;

		while (afs_select_fileserver(&fc)) {
			fc.cb_break = afs_calc_vnode_cb_break(dvnode);
			afs_fs_rename(&fc, old->d_name.name,
				      dvnode, new->d_name.name,
				      scb, scb);
		}

		afs_vnode_commit_status(&fc, dvnode, fc.cb_break,
					&dir_data_version, scb);
		ret = afs_end_vnode_operation(&fc);
	}

	if (ret == 0) {
		spin_lock(&old->d_lock);
		old->d_flags |= DCACHE_NFSFS_RENAMED;
		spin_unlock(&old->d_lock);
		if (dvnode->silly_key != key) {
			key_put(dvnode->silly_key);
			dvnode->silly_key = key_get(key);
		}

		down_write(&dvnode->validate_lock);
		if (test_bit(AFS_VNODE_DIR_VALID, &dvnode->flags) &&
		    dvnode->status.data_version == dir_data_version) {
			afs_edit_dir_remove(dvnode, &old->d_name,
					    afs_edit_dir_for_silly_0);
			afs_edit_dir_add(dvnode, &new->d_name,
					 &vnode->fid, afs_edit_dir_for_silly_1);
		}
		up_write(&dvnode->validate_lock);
	}

	kfree(scb);
	_leave(" = %d", ret);
	return ret;
}

/**
 * afs_sillyrename - Perform a silly-rename of a dentry
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

	ihold(&vnode->vfs_inode);

	ret = afs_do_silly_rename(dvnode, vnode, dentry, sdentry, key);
	switch (ret) {
	case 0:
		/* The rename succeeded. */
		d_move(dentry, sdentry);
		break;
	case -ERESTARTSYS:
		/* The result of the rename is unknown. Play it safe by forcing
		 * a new lookup.
		 */
		d_drop(dentry);
		d_drop(sdentry);
	}

	iput(&vnode->vfs_inode);
	dput(sdentry);
out:
	_leave(" = %d", ret);
	return ret;
}

/*
 * Tell the server to remove a sillyrename file.
 */
static int afs_do_silly_unlink(struct afs_vnode *dvnode, struct afs_vnode *vnode,
			       struct dentry *dentry, struct key *key)
{
	struct afs_fs_cursor fc;
	struct afs_status_cb *scb;
	int ret = -ERESTARTSYS;

	_enter("");

	scb = kcalloc(2, sizeof(struct afs_status_cb), GFP_KERNEL);
	if (!scb)
		return -ENOMEM;

	trace_afs_silly_rename(vnode, true);
	if (afs_begin_vnode_operation(&fc, dvnode, key, false)) {
		afs_dataversion_t dir_data_version = dvnode->status.data_version + 1;

		while (afs_select_fileserver(&fc)) {
			fc.cb_break = afs_calc_vnode_cb_break(dvnode);

			if (test_bit(AFS_SERVER_FL_IS_YFS, &fc.cbi->server->flags) &&
			    !test_bit(AFS_SERVER_FL_NO_RM2, &fc.cbi->server->flags)) {
				yfs_fs_remove_file2(&fc, vnode, dentry->d_name.name,
						    &scb[0], &scb[1]);
				if (fc.ac.error != -ECONNABORTED ||
				    fc.ac.abort_code != RXGEN_OPCODE)
					continue;
				set_bit(AFS_SERVER_FL_NO_RM2, &fc.cbi->server->flags);
			}

			afs_fs_remove(&fc, vnode, dentry->d_name.name, false, &scb[0]);
		}

		afs_vnode_commit_status(&fc, dvnode, fc.cb_break,
					&dir_data_version, &scb[0]);
		ret = afs_end_vnode_operation(&fc);
		if (ret == 0) {
			drop_nlink(&vnode->vfs_inode);
			if (vnode->vfs_inode.i_nlink == 0) {
				set_bit(AFS_VNODE_DELETED, &vnode->flags);
				clear_bit(AFS_VNODE_CB_PROMISED, &vnode->flags);
			}
		}
		if (ret == 0) {
			down_write(&dvnode->validate_lock);
			if (test_bit(AFS_VNODE_DIR_VALID, &dvnode->flags) &&
			    dvnode->status.data_version == dir_data_version)
				afs_edit_dir_remove(dvnode, &dentry->d_name,
						    afs_edit_dir_for_unlink);
			up_write(&dvnode->validate_lock);
		}
	}

	kfree(scb);
	_leave(" = %d", ret);
	return ret;
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
