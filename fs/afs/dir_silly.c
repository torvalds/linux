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
#include <linux/fsyestify.h>
#include "internal.h"

/*
 * Actually perform the silly rename step.
 */
static int afs_do_silly_rename(struct afs_vyesde *dvyesde, struct afs_vyesde *vyesde,
			       struct dentry *old, struct dentry *new,
			       struct key *key)
{
	struct afs_fs_cursor fc;
	struct afs_status_cb *scb;
	int ret = -ERESTARTSYS;

	_enter("%pd,%pd", old, new);

	scb = kzalloc(sizeof(struct afs_status_cb), GFP_KERNEL);
	if (!scb)
		return -ENOMEM;

	trace_afs_silly_rename(vyesde, false);
	if (afs_begin_vyesde_operation(&fc, dvyesde, key, true)) {
		afs_dataversion_t dir_data_version = dvyesde->status.data_version + 1;

		while (afs_select_fileserver(&fc)) {
			fc.cb_break = afs_calc_vyesde_cb_break(dvyesde);
			afs_fs_rename(&fc, old->d_name.name,
				      dvyesde, new->d_name.name,
				      scb, scb);
		}

		afs_vyesde_commit_status(&fc, dvyesde, fc.cb_break,
					&dir_data_version, scb);
		ret = afs_end_vyesde_operation(&fc);
	}

	if (ret == 0) {
		spin_lock(&old->d_lock);
		old->d_flags |= DCACHE_NFSFS_RENAMED;
		spin_unlock(&old->d_lock);
		if (dvyesde->silly_key != key) {
			key_put(dvyesde->silly_key);
			dvyesde->silly_key = key_get(key);
		}

		if (test_bit(AFS_VNODE_DIR_VALID, &dvyesde->flags))
			afs_edit_dir_remove(dvyesde, &old->d_name,
					    afs_edit_dir_for_silly_0);
		if (test_bit(AFS_VNODE_DIR_VALID, &dvyesde->flags))
			afs_edit_dir_add(dvyesde, &new->d_name,
					 &vyesde->fid, afs_edit_dir_for_silly_1);
	}

	kfree(scb);
	_leave(" = %d", ret);
	return ret;
}

/**
 * afs_sillyrename - Perform a silly-rename of a dentry
 *
 * AFS is stateless and the server doesn't kyesw when the client is holding a
 * file open.  To prevent application problems when a file is unlinked while
 * it's still open, the client performs a "silly-rename".  That is, it renames
 * the file to a hidden file in the same directory, and only performs the
 * unlink once the last reference to it is put.
 *
 * The final cleanup is done during dentry_iput.
 */
int afs_sillyrename(struct afs_vyesde *dvyesde, struct afs_vyesde *vyesde,
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
		 * understood by the salvager and must yest be changed.
		 */
		slen = scnprintf(silly, sizeof(silly), ".__afs%04X", sillycounter);
		sdentry = lookup_one_len(silly, dentry->d_parent, slen);

		/* N.B. Better to return EBUSY here ... it could be dangerous
		 * to delete the file while it's in use.
		 */
		if (IS_ERR(sdentry))
			goto out;
	} while (!d_is_negative(sdentry));

	ihold(&vyesde->vfs_iyesde);

	ret = afs_do_silly_rename(dvyesde, vyesde, dentry, sdentry, key);
	switch (ret) {
	case 0:
		/* The rename succeeded. */
		d_move(dentry, sdentry);
		break;
	case -ERESTARTSYS:
		/* The result of the rename is unkyeswn. Play it safe by forcing
		 * a new lookup.
		 */
		d_drop(dentry);
		d_drop(sdentry);
	}

	iput(&vyesde->vfs_iyesde);
	dput(sdentry);
out:
	_leave(" = %d", ret);
	return ret;
}

/*
 * Tell the server to remove a sillyrename file.
 */
static int afs_do_silly_unlink(struct afs_vyesde *dvyesde, struct afs_vyesde *vyesde,
			       struct dentry *dentry, struct key *key)
{
	struct afs_fs_cursor fc;
	struct afs_status_cb *scb;
	int ret = -ERESTARTSYS;

	_enter("");

	scb = kcalloc(2, sizeof(struct afs_status_cb), GFP_KERNEL);
	if (!scb)
		return -ENOMEM;

	trace_afs_silly_rename(vyesde, true);
	if (afs_begin_vyesde_operation(&fc, dvyesde, key, false)) {
		afs_dataversion_t dir_data_version = dvyesde->status.data_version + 1;

		while (afs_select_fileserver(&fc)) {
			fc.cb_break = afs_calc_vyesde_cb_break(dvyesde);

			if (test_bit(AFS_SERVER_FL_IS_YFS, &fc.cbi->server->flags) &&
			    !test_bit(AFS_SERVER_FL_NO_RM2, &fc.cbi->server->flags)) {
				yfs_fs_remove_file2(&fc, vyesde, dentry->d_name.name,
						    &scb[0], &scb[1]);
				if (fc.ac.error != -ECONNABORTED ||
				    fc.ac.abort_code != RXGEN_OPCODE)
					continue;
				set_bit(AFS_SERVER_FL_NO_RM2, &fc.cbi->server->flags);
			}

			afs_fs_remove(&fc, vyesde, dentry->d_name.name, false, &scb[0]);
		}

		afs_vyesde_commit_status(&fc, dvyesde, fc.cb_break,
					&dir_data_version, &scb[0]);
		ret = afs_end_vyesde_operation(&fc);
		if (ret == 0) {
			drop_nlink(&vyesde->vfs_iyesde);
			if (vyesde->vfs_iyesde.i_nlink == 0) {
				set_bit(AFS_VNODE_DELETED, &vyesde->flags);
				clear_bit(AFS_VNODE_CB_PROMISED, &vyesde->flags);
			}
		}
		if (ret == 0 &&
		    test_bit(AFS_VNODE_DIR_VALID, &dvyesde->flags))
			afs_edit_dir_remove(dvyesde, &dentry->d_name,
					    afs_edit_dir_for_unlink);
	}

	kfree(scb);
	_leave(" = %d", ret);
	return ret;
}

/*
 * Remove sillyrename file on iput.
 */
int afs_silly_iput(struct dentry *dentry, struct iyesde *iyesde)
{
	struct afs_vyesde *dvyesde = AFS_FS_I(d_iyesde(dentry->d_parent));
	struct afs_vyesde *vyesde = AFS_FS_I(iyesde);
	struct dentry *alias;
	int ret;

	DECLARE_WAIT_QUEUE_HEAD_ONSTACK(wq);

	_enter("%p{%pd},%llx", dentry, dentry, vyesde->fid.vyesde);

	down_read(&dvyesde->rmdir_lock);

	alias = d_alloc_parallel(dentry->d_parent, &dentry->d_name, &wq);
	if (IS_ERR(alias)) {
		up_read(&dvyesde->rmdir_lock);
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
		up_read(&dvyesde->rmdir_lock);
		dput(alias);
		return ret;
	}

	/* Stop lock-release from complaining. */
	spin_lock(&vyesde->lock);
	vyesde->lock_state = AFS_VNODE_LOCK_DELETED;
	trace_afs_flock_ev(vyesde, NULL, afs_flock_silly_delete, 0);
	spin_unlock(&vyesde->lock);

	afs_do_silly_unlink(dvyesde, vyesde, dentry, dvyesde->silly_key);
	up_read(&dvyesde->rmdir_lock);
	d_lookup_done(alias);
	dput(alias);
	return 1;
}
