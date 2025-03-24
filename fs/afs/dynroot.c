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

	netfs_inode_init(&vnode->netfs, NULL, false);
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
	simple_inode_init_ts(inode);
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
	char *result = NULL;
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
			&result, NULL, false);
	if (ret == -ENODATA || ret == -ENOKEY || ret == 0)
		ret = -ENOENT;
	if (ret > 0 && ret >= sizeof(struct dns_server_list_v1_header)) {
		struct dns_server_list_v1_header *v1 = (void *)result;

		if (v1->hdr.zero == 0 &&
		    v1->hdr.content == DNS_PAYLOAD_IS_SERVER_LIST &&
		    v1->hdr.version == 1 &&
		    (v1->status != DNS_LOOKUP_GOOD &&
		     v1->status != DNS_LOOKUP_GOOD_WITH_BAD))
			return -ENOENT;

	}

	kfree(result);
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

	return d_splice_alias(afs_try_auto_mntpt(dentry, dir), dentry);
}

const struct inode_operations afs_dynroot_inode_operations = {
	.lookup		= afs_dynroot_lookup,
};

const struct dentry_operations afs_dynroot_dentry_operations = {
	.d_delete	= always_delete_dentry,
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
	struct dentry *root, *subdir, *dsubdir;
	char *dotname = cell->name - 1;
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

	dsubdir = lookup_one_len(dotname, root, cell->name_len + 1);
	if (IS_ERR(dsubdir)) {
		ret = PTR_ERR(dsubdir);
		dput(subdir);
		goto unlock;
	}

	/* Note that we're retaining extra refs on the dentries. */
	subdir->d_fsdata = (void *)1UL;
	dsubdir->d_fsdata = (void *)1UL;
	ret = 0;
unlock:
	inode_unlock(root->d_inode);
	return ret;
}

static void afs_dynroot_rm_one_dir(struct dentry *root, const char *name, size_t name_len)
{
	struct dentry *subdir;

	/* Don't want to trigger a lookup call, which will re-add the cell */
	subdir = try_lookup_one_len(name, root, name_len);
	if (IS_ERR_OR_NULL(subdir)) {
		_debug("lookup %ld", PTR_ERR(subdir));
		return;
	}

	_debug("rmdir %pd %u", subdir, d_count(subdir));

	if (subdir->d_fsdata) {
		_debug("unpin %u", d_count(subdir));
		subdir->d_fsdata = NULL;
		dput(subdir);
	}
	dput(subdir);
}

/*
 * Remove a manually added cell mount directory.
 * - The caller must hold net->proc_cells_lock
 */
void afs_dynroot_rmdir(struct afs_net *net, struct afs_cell *cell)
{
	struct super_block *sb = net->dynroot_sb;
	char *dotname = cell->name - 1;

	if (!sb || atomic_read(&sb->s_active) == 0)
		return;

	inode_lock(sb->s_root->d_inode);
	afs_dynroot_rm_one_dir(sb->s_root, cell->name, cell->name_len);
	afs_dynroot_rm_one_dir(sb->s_root, dotname, cell->name_len + 1);
	inode_unlock(sb->s_root->d_inode);
	_leave("");
}

static void afs_atcell_delayed_put_cell(void *arg)
{
	struct afs_cell *cell = arg;

	afs_put_cell(cell, afs_cell_trace_put_atcell);
}

/*
 * Read @cell or .@cell symlinks.
 */
static const char *afs_atcell_get_link(struct dentry *dentry, struct inode *inode,
				       struct delayed_call *done)
{
	struct afs_vnode *vnode = AFS_FS_I(inode);
	struct afs_cell *cell;
	struct afs_net *net = afs_i2net(inode);
	const char *name;
	bool dotted = vnode->fid.vnode == 3;

	if (!rcu_access_pointer(net->ws_cell))
		return ERR_PTR(-ENOENT);

	if (!dentry) {
		/* We're in RCU-pathwalk. */
		cell = rcu_dereference(net->ws_cell);
		if (dotted)
			name = cell->name - 1;
		else
			name = cell->name;
		/* Shouldn't need to set a delayed call. */
		return name;
	}

	down_read(&net->cells_lock);

	cell = rcu_dereference_protected(net->ws_cell, lockdep_is_held(&net->cells_lock));
	if (dotted)
		name = cell->name - 1;
	else
		name = cell->name;
	afs_get_cell(cell, afs_cell_trace_get_atcell);
	set_delayed_call(done, afs_atcell_delayed_put_cell, cell);

	up_read(&net->cells_lock);
	return name;
}

static const struct inode_operations afs_atcell_inode_operations = {
	.get_link	= afs_atcell_get_link,
};

/*
 * Look up @cell or .@cell in a dynroot directory.  This is a substitution for
 * the local cell name for the net namespace.
 */
static struct dentry *afs_dynroot_create_symlink(struct dentry *root, const char *name)
{
	struct afs_vnode *vnode;
	struct afs_fid fid = { .vnode = 2, .unique = 1, };
	struct dentry *dentry;
	struct inode *inode;

	if (name[0] == '.')
		fid.vnode = 3;

	dentry = d_alloc_name(root, name);
	if (!dentry)
		return ERR_PTR(-ENOMEM);

	inode = iget5_locked(dentry->d_sb, fid.vnode,
			     afs_iget5_pseudo_test, afs_iget5_pseudo_set, &fid);
	if (!inode) {
		dput(dentry);
		return ERR_PTR(-ENOMEM);
	}

	vnode = AFS_FS_I(inode);

	/* there shouldn't be an existing inode */
	if (WARN_ON_ONCE(!(inode->i_state & I_NEW))) {
		iput(inode);
		dput(dentry);
		return ERR_PTR(-EIO);
	}

	netfs_inode_init(&vnode->netfs, NULL, false);
	simple_inode_init_ts(inode);
	set_nlink(inode, 1);
	inode->i_size		= 0;
	inode->i_mode		= S_IFLNK | 0555;
	inode->i_op		= &afs_atcell_inode_operations;
	inode->i_uid		= GLOBAL_ROOT_UID;
	inode->i_gid		= GLOBAL_ROOT_GID;
	inode->i_blocks		= 0;
	inode->i_generation	= 0;
	inode->i_flags		|= S_NOATIME;

	unlock_new_inode(inode);
	d_splice_alias(inode, dentry);
	return dentry;
}

/*
 * Create @cell and .@cell symlinks.
 */
static int afs_dynroot_symlink(struct afs_net *net)
{
	struct super_block *sb = net->dynroot_sb;
	struct dentry *root, *symlink, *dsymlink;
	int ret;

	/* Let the ->lookup op do the creation */
	root = sb->s_root;
	inode_lock(root->d_inode);
	symlink = afs_dynroot_create_symlink(root, "@cell");
	if (IS_ERR(symlink)) {
		ret = PTR_ERR(symlink);
		goto unlock;
	}

	dsymlink = afs_dynroot_create_symlink(root, ".@cell");
	if (IS_ERR(dsymlink)) {
		ret = PTR_ERR(dsymlink);
		dput(symlink);
		goto unlock;
	}

	/* Note that we're retaining extra refs on the dentries. */
	symlink->d_fsdata = (void *)1UL;
	dsymlink->d_fsdata = (void *)1UL;
	ret = 0;
unlock:
	inode_unlock(root->d_inode);
	return ret;
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
	ret = afs_dynroot_symlink(net);
	if (ret < 0)
		goto error;

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
	struct dentry *root = sb->s_root, *subdir;

	/* Prevent more subdirs from being created */
	mutex_lock(&net->proc_cells_lock);
	if (net->dynroot_sb == sb)
		net->dynroot_sb = NULL;
	mutex_unlock(&net->proc_cells_lock);

	if (root) {
		struct hlist_node *n;
		inode_lock(root->d_inode);

		/* Remove all the pins for dirs created for manually added cells */
		hlist_for_each_entry_safe(subdir, n, &root->d_children, d_sib) {
			if (subdir->d_fsdata) {
				subdir->d_fsdata = NULL;
				dput(subdir);
			}
		}

		inode_unlock(root->d_inode);
	}
}
