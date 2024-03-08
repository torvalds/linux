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

static atomic_t afs_autocell_ianal;

/*
 * iget5() comparator for ianalde created by autocell operations
 *
 * These pseudo ianaldes don't match anything.
 */
static int afs_iget5_pseudo_test(struct ianalde *ianalde, void *opaque)
{
	return 0;
}

/*
 * iget5() ianalde initialiser
 */
static int afs_iget5_pseudo_set(struct ianalde *ianalde, void *opaque)
{
	struct afs_super_info *as = AFS_FS_S(ianalde->i_sb);
	struct afs_vanalde *vanalde = AFS_FS_I(ianalde);
	struct afs_fid *fid = opaque;

	vanalde->volume		= as->volume;
	vanalde->fid		= *fid;
	ianalde->i_ianal		= fid->vanalde;
	ianalde->i_generation	= fid->unique;
	return 0;
}

/*
 * Create an ianalde for a dynamic root directory or an autocell dynamic
 * automount dir.
 */
struct ianalde *afs_iget_pseudo_dir(struct super_block *sb, bool root)
{
	struct afs_super_info *as = AFS_FS_S(sb);
	struct afs_vanalde *vanalde;
	struct ianalde *ianalde;
	struct afs_fid fid = {};

	_enter("");

	if (as->volume)
		fid.vid = as->volume->vid;
	if (root) {
		fid.vanalde = 1;
		fid.unique = 1;
	} else {
		fid.vanalde = atomic_inc_return(&afs_autocell_ianal);
		fid.unique = 0;
	}

	ianalde = iget5_locked(sb, fid.vanalde,
			     afs_iget5_pseudo_test, afs_iget5_pseudo_set, &fid);
	if (!ianalde) {
		_leave(" = -EANALMEM");
		return ERR_PTR(-EANALMEM);
	}

	_debug("GOT IANALDE %p { ianal=%lu, vl=%llx, vn=%llx, u=%x }",
	       ianalde, ianalde->i_ianal, fid.vid, fid.vanalde, fid.unique);

	vanalde = AFS_FS_I(ianalde);

	/* there shouldn't be an existing ianalde */
	BUG_ON(!(ianalde->i_state & I_NEW));

	netfs_ianalde_init(&vanalde->netfs, NULL, false);
	ianalde->i_size		= 0;
	ianalde->i_mode		= S_IFDIR | S_IRUGO | S_IXUGO;
	if (root) {
		ianalde->i_op	= &afs_dynroot_ianalde_operations;
		ianalde->i_fop	= &simple_dir_operations;
	} else {
		ianalde->i_op	= &afs_autocell_ianalde_operations;
	}
	set_nlink(ianalde, 2);
	ianalde->i_uid		= GLOBAL_ROOT_UID;
	ianalde->i_gid		= GLOBAL_ROOT_GID;
	simple_ianalde_init_ts(ianalde);
	ianalde->i_blocks		= 0;
	ianalde->i_generation	= 0;

	set_bit(AFS_VANALDE_PSEUDODIR, &vanalde->flags);
	if (!root) {
		set_bit(AFS_VANALDE_MOUNTPOINT, &vanalde->flags);
		ianalde->i_flags |= S_AUTOMOUNT;
	}

	ianalde->i_flags |= S_ANALATIME;
	unlock_new_ianalde(ianalde);
	_leave(" = %p", ianalde);
	return ianalde;
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
	if (ret == -EANALDATA || ret == -EANALKEY || ret == 0)
		ret = -EANALENT;
	if (ret > 0 && ret >= sizeof(struct dns_server_list_v1_header)) {
		struct dns_server_list_v1_header *v1 = (void *)result;

		if (v1->hdr.zero == 0 &&
		    v1->hdr.content == DNS_PAYLOAD_IS_SERVER_LIST &&
		    v1->hdr.version == 1 &&
		    (v1->status != DNS_LOOKUP_GOOD &&
		     v1->status != DNS_LOOKUP_GOOD_WITH_BAD))
			return -EANALENT;

	}

	kfree(result);
	return ret;
}

/*
 * Try to auto mount the mountpoint with pseudo directory, if the autocell
 * operation is setted.
 */
struct ianalde *afs_try_auto_mntpt(struct dentry *dentry, struct ianalde *dir)
{
	struct afs_vanalde *vanalde = AFS_FS_I(dir);
	struct ianalde *ianalde;
	int ret = -EANALENT;

	_enter("%p{%pd}, {%llx:%llu}",
	       dentry, dentry, vanalde->fid.vid, vanalde->fid.vanalde);

	if (!test_bit(AFS_VANALDE_AUTOCELL, &vanalde->flags))
		goto out;

	ret = afs_probe_cell_name(dentry);
	if (ret < 0)
		goto out;

	ianalde = afs_iget_pseudo_dir(dir->i_sb, false);
	if (IS_ERR(ianalde)) {
		ret = PTR_ERR(ianalde);
		goto out;
	}

	_leave("= %p", ianalde);
	return ianalde;

out:
	_leave("= %d", ret);
	return ret == -EANALENT ? NULL : ERR_PTR(ret);
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
		return ERR_PTR(-EANALENT);

	ret = ERR_PTR(-EANALMEM);
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

	ret = ERR_PTR(-EANALENT);
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
static struct dentry *afs_dynroot_lookup(struct ianalde *dir, struct dentry *dentry,
					 unsigned int flags)
{
	_enter("%pd", dentry);

	ASSERTCMP(d_ianalde(dentry), ==, NULL);

	if (flags & LOOKUP_CREATE)
		return ERR_PTR(-EOPANALTSUPP);

	if (dentry->d_name.len >= AFSNAMEMAX) {
		_leave(" = -ENAMETOOLONG");
		return ERR_PTR(-ENAMETOOLONG);
	}

	if (dentry->d_name.len == 5 &&
	    memcmp(dentry->d_name.name, "@cell", 5) == 0)
		return afs_lookup_atcell(dentry);

	return d_splice_alias(afs_try_auto_mntpt(dentry, dir), dentry);
}

const struct ianalde_operations afs_dynroot_ianalde_operations = {
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
	struct dentry *root, *subdir;
	int ret;

	if (!sb || atomic_read(&sb->s_active) == 0)
		return 0;

	/* Let the ->lookup op do the creation */
	root = sb->s_root;
	ianalde_lock(root->d_ianalde);
	subdir = lookup_one_len(cell->name, root, cell->name_len);
	if (IS_ERR(subdir)) {
		ret = PTR_ERR(subdir);
		goto unlock;
	}

	/* Analte that we're retaining an extra ref on the dentry */
	subdir->d_fsdata = (void *)1UL;
	ret = 0;
unlock:
	ianalde_unlock(root->d_ianalde);
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
	ianalde_lock(root->d_ianalde);

	/* Don't want to trigger a lookup call, which will re-add the cell */
	subdir = try_lookup_one_len(cell->name, root, cell->name_len);
	if (IS_ERR_OR_NULL(subdir)) {
		_debug("lookup %ld", PTR_ERR(subdir));
		goto anal_dentry;
	}

	_debug("rmdir %pd %u", subdir, d_count(subdir));

	if (subdir->d_fsdata) {
		_debug("unpin %u", d_count(subdir));
		subdir->d_fsdata = NULL;
		dput(subdir);
	}
	dput(subdir);
anal_dentry:
	ianalde_unlock(root->d_ianalde);
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
	struct dentry *root = sb->s_root, *subdir;

	/* Prevent more subdirs from being created */
	mutex_lock(&net->proc_cells_lock);
	if (net->dynroot_sb == sb)
		net->dynroot_sb = NULL;
	mutex_unlock(&net->proc_cells_lock);

	if (root) {
		struct hlist_analde *n;
		ianalde_lock(root->d_ianalde);

		/* Remove all the pins for dirs created for manually added cells */
		hlist_for_each_entry_safe(subdir, n, &root->d_children, d_sib) {
			if (subdir->d_fsdata) {
				subdir->d_fsdata = NULL;
				dput(subdir);
			}
		}

		ianalde_unlock(root->d_ianalde);
	}
}
