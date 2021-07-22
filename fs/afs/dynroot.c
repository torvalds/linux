// SPDX-License-Identifier: GPL-2.0-or-later
/* AFS dynamic root handling
 *
 * Copyright (C) 2018 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/fs.h>
#include <linux/namei.h>
#include <linux/dns_resolver.h>
#include "internal.h"

static atomic_t afs_autocell_ino;

/*
 * iget5() comparator for inode created by autocell operations
 *
 * These pseudo inodes don't match anything.
 */
static int afs_iget5_pseudo_test(struct inode *inode, void *opaque)
{
	return 0;
}

/*
 * iget5() inode initialiser
 */
static int afs_iget5_pseudo_set(struct inode *inode, void *opaque)
{
	struct afs_super_info *as = AFS_FS_S(inode->i_sb);
	struct afs_vnode *vnode = AFS_FS_I(inode);
	struct afs_fid *fid = opaque;

	vnode->volume		= as->volume;
	vnode->fid		= *fid;
	inode->i_ino		= fid->vnode;
	inode->i_generation	= fid->unique;
	return 0;
}

/*
 * Create an inode for a dynamic root directory or an autocell dynamic
 * automount dir.
 */
struct inode *afs_iget_pseudo_dir(struct super_block *sb, bool root)
{
	struct afs_super_info *as = AFS_FS_S(sb);
	struct afs_vnode *vnode;
	struct inode *inode;
	struct afs_fid fid = {};

	_enter("");

	if (as->volume)
		fid.vid = as->volume->vid;
	if (root) {
		fid.vnode = 1;
		fid.unique = 1;
	} else {
		fid.vnode = atomic_inc_return(&afs_autocell_ino);
		fid.unique = 0;
	}

	inode = iget5_locked(sb, fid.vnode,
			     afs_iget5_pseudo_test, afs_iget5_pseudo_set, &fid);
	if (!inode) {
		_leave(" = -ENOMEM");
		return ERR_PTR(-ENOMEM);
	}

	_debug("GOT INODE %p { ino=%lu, vl=%llx, vn=%llx, u=%x }",
	       inode, inode->i_ino, fid.vid, fid.vnode, fid.unique);

	vnode = AFS_FS_I(inode);

	/* there shouldn't be an existing inode */
	BUG_ON(!(inode->i_state & I_NEW));

	inode->i_size		= 0;
	inode->i_mode		= S_IFDIR | S_IRUGO | S_IXUGO;
	if (root) {
		inode->i_op	= &afs_dynroot_inode_operations;
		inode->i_fop	= &simple_dir_operations;
	} else {
		inode->i_op	= &afs_autocell_inode_operations;
	}
	set_nlink(inode, 2);
	inode->i_uid		= GLOBAL_ROOT_UID;
	inode->i_gid		= GLOBAL_ROOT_GID;
	inode->i_ctime = inode->i_atime = inode->i_mtime = current_time(inode);
	inode->i_blocks		= 0;
	inode->i_generation	= 0;

	set_bit(AFS_VNODE_PSEUDODIR, &vnode->flags);
	if (!root) {
		set_bit(AFS_VNODE_MOUNTPOINT, &vnode->flags);
		inode->i_flags |= S_AUTOMOUNT;
	}

	inode->i_flags |= S_NOATIME;
	unlock_new_inode(inode);
	_leave(" = %p", inode);
	return inode;
}

/*
 * Probe to see if a cell may exist.  This prevents positive dentries from
 * being created unnecessarily.
 */
static int afs_probe_cell_name(struct dentry *dentry)
{
	struct afs_cell *cell;
	struct afs_net *net = afs_d2net(dentry);
	const char *name = dentry->d_name.name;
	size_t len = dentry->d_name.len;
	int ret;

	/* Names prefixed with a dot are R/W mounts. */
	if (name[0] == '.') {
		if (len == 1)
			return -EINVAL;
		name++;
		len--;
	}

	cell = afs_find_cell(net, name, len, afs_cell_trace_use_probe);
	if (!IS_ERR(cell)) {
		afs_unuse_cell(net, cell, afs_cell_trace_unuse_probe);
		return 0;
	}

	ret = dns_query(net->net, "afsdb", name, len, "srv=1",
			NULL, NULL, false);
	if (ret == -ENODATA)
		ret = -EDESTADDRREQ;
	return ret;
}

/*
 * Try to auto mount the mountpoint with pseudo directory, if the autocell
 * operation is setted.
 */
struct inode *afs_try_auto_mntpt(struct dentry *dentry, struct inode *dir)
{
	struct afs_vnode *vnode = AFS_FS_I(dir);
	struct inode *inode;
	int ret = -ENOENT;

	_enter("%p{%pd}, {%llx:%llu}",
	       dentry, dentry, vnode->fid.vid, vnode->fid.vnode);

	if (!test_bit(AFS_VNODE_AUTOCELL, &vnode->flags))
		goto out;

	ret = afs_probe_cell_name(dentry);
	if (ret < 0)
		goto out;

	inode = afs_iget_pseudo_dir(dir->i_sb, false);
	if (IS_ERR(inode)) {
		ret = PTR_ERR(inode);
		goto out;
	}

	_leave("= %p", inode);
	return inode;

out:
	_leave("= %d", ret);
	return ret == -ENOENT ? NULL : ERR_PTR(ret);
}

/*
 * Look up @cell in a dynroot directory.  This is a substitution for the
 * local cell name for the net namespace.
 */
static struct dentry *afs_lookup_atcell(struct dentry *dentry)
{
	struct afs_cell *cell;
	struct afs_net *net = afs_d2net(dentry);
	struct dentry *ret;
	char *name;
	int len;

	if (!net->ws_cell)
		return ERR_PTR(-ENOENT);

	ret = ERR_PTR(-ENOMEM);
	name = kmalloc(AFS_MAXCELLNAME + 1, GFP_KERNEL);
	if (!name)
		goto out_p;

	down_read(&net->cells_lock);
	cell = net->ws_cell;
	if (cell) {
		len = cell->name_len;
		memcpy(name, cell->name, len + 1);
	}
	up_read(&net->cells_lock);

	ret = ERR_PTR(-ENOENT);
	if (!cell)
		goto out_n;

	ret = lookup_one_len(name, dentry->d_parent, len);

	/* We don't want to d_add() the @cell dentry here as we don't want to
	 * the cached dentry to hide changes to the local cell name.
	 */

out_n:
	kfree(name);
out_p:
	return ret;
}

/*
 * Look up an entry in a dynroot directory.
 */
static struct dentry *afs_dynroot_lookup(struct inode *dir, struct dentry *dentry,
					 unsigned int flags)
{
	_enter("%pd", dentry);

	ASSERTCMP(d_inode(dentry), ==, NULL);

	if (flags & LOOKUP_CREATE)
		return ERR_PTR(-EOPNOTSUPP);

	if (dentry->d_name.len >= AFSNAMEMAX) {
		_leave(" = -ENAMETOOLONG");
		return ERR_PTR(-ENAMETOOLONG);
	}

	if (dentry->d_name.len == 5 &&
	    memcmp(dentry->d_name.name, "@cell", 5) == 0)
		return afs_lookup_atcell(dentry);

	return d_splice_alias(afs_try_auto_mntpt(dentry, dir), dentry);
}

const struct inode_operations afs_dynroot_inode_operations = {
	.lookup		= afs_dynroot_lookup,
};

/*
 * Dirs in the dynamic root don't need revalidation.
 */
static int afs_dynroot_d_revalidate(struct dentry *dentry, unsigned int flags)
{
	return 1;
}

/*
 * Allow the VFS to enquire as to whether a dentry should be unhashed (mustn't
 * sleep)
 * - called from dput() when d_count is going to 0.
 * - return 1 to request dentry be unhashed, 0 otherwise
 */
static int afs_dynroot_d_delete(const struct dentry *dentry)
{
	return d_really_is_positive(dentry);
}

const struct dentry_operations afs_dynroot_dentry_operations = {
	.d_revalidate	= afs_dynroot_d_revalidate,
	.d_delete	= afs_dynroot_d_delete,
	.d_release	= afs_d_release,
	.d_automount	= afs_d_automount,
};

/*
 * Create a manually added cell mount directory.
 * - The caller must hold net->proc_cells_lock
 */
int afs_dynroot_mkdir(struct afs_net *net, struct afs_cell *cell)
{
	struct super_block *sb = net->dynroot_sb;
	struct dentry *root, *subdir;
	int ret;

	if (!sb || atomic_read(&sb->s_active) == 0)
		return 0;

	/* Let the ->lookup op do the creation */
	root = sb->s_root;
	inode_lock(root->d_inode);
	subdir = lookup_one_len(cell->name, root, cell->name_len);
	if (IS_ERR(subdir)) {
		ret = PTR_ERR(subdir);
		goto unlock;
	}

	/* Note that we're retaining an extra ref on the dentry */
	subdir->d_fsdata = (void *)1UL;
	ret = 0;
unlock:
	inode_unlock(root->d_inode);
	return ret;
}

/*
 * Remove a manually added cell mount directory.
 * - The caller must hold net->proc_cells_lock
 */
void afs_dynroot_rmdir(struct afs_net *net, struct afs_cell *cell)
{
	struct super_block *sb = net->dynroot_sb;
	struct dentry *root, *subdir;

	if (!sb || atomic_read(&sb->s_active) == 0)
		return;

	root = sb->s_root;
	inode_lock(root->d_inode);

	/* Don't want to trigger a lookup call, which will re-add the cell */
	subdir = try_lookup_one_len(cell->name, root, cell->name_len);
	if (IS_ERR_OR_NULL(subdir)) {
		_debug("lookup %ld", PTR_ERR(subdir));
		goto no_dentry;
	}

	_debug("rmdir %pd %u", subdir, d_count(subdir));

	if (subdir->d_fsdata) {
		_debug("unpin %u", d_count(subdir));
		subdir->d_fsdata = NULL;
		dput(subdir);
	}
	dput(subdir);
no_dentry:
	inode_unlock(root->d_inode);
	_leave("");
}

/*
 * Populate a newly created dynamic root with cell names.
 */
int afs_dynroot_populate(struct super_block *sb)
{
	struct afs_cell *cell;
	struct afs_net *net = afs_sb2net(sb);
	int ret;

	mutex_lock(&net->proc_cells_lock);

	net->dynroot_sb = sb;
	hlist_for_each_entry(cell, &net->proc_cells, proc_link) {
		ret = afs_dynroot_mkdir(net, cell);
		if (ret < 0)
			goto error;
	}

	ret = 0;
out:
	mutex_unlock(&net->proc_cells_lock);
	return ret;

error:
	net->dynroot_sb = NULL;
	goto out;
}

/*
 * When a dynamic root that's in the process of being destroyed, depopulate it
 * of pinned directories.
 */
void afs_dynroot_depopulate(struct super_block *sb)
{
	struct afs_net *net = afs_sb2net(sb);
	struct dentry *root = sb->s_root, *subdir, *tmp;

	/* Prevent more subdirs from being created */
	mutex_lock(&net->proc_cells_lock);
	if (net->dynroot_sb == sb)
		net->dynroot_sb = NULL;
	mutex_unlock(&net->proc_cells_lock);

	if (root) {
		inode_lock(root->d_inode);

		/* Remove all the pins for dirs created for manually added cells */
		list_for_each_entry_safe(subdir, tmp, &root->d_subdirs, d_child) {
			if (subdir->d_fsdata) {
				subdir->d_fsdata = NULL;
				dput(subdir);
			}
		}

		inode_unlock(root->d_inode);
	}
}
