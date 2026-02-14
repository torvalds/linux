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

#define AFS_MIN_DYNROOT_CELL_INO 4 /* Allow for ., .., @cell, .@cell */
#define AFS_MAX_DYNROOT_CELL_INO ((unsigned int)INT_MAX)

static struct dentry *afs_lookup_atcell(struct inode *dir, struct dentry *dentry, ino_t ino);

/*
 * iget5() comparator for inode created by autocell operations
 */
static int afs_iget5_pseudo_test(struct inode *inode, void *opaque)
{
	struct afs_fid *fid = opaque;

	return inode->i_ino == fid->vnode;
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
 * Create an inode for an autocell dynamic automount dir.
 */
static struct inode *afs_iget_pseudo_dir(struct super_block *sb, ino_t ino)
{
	struct afs_vnode *vnode;
	struct inode *inode;
	struct afs_fid fid = { .vnode = ino, .unique = 1, };

	_enter("");

	inode = iget5_locked(sb, fid.vnode,
			     afs_iget5_pseudo_test, afs_iget5_pseudo_set, &fid);
	if (!inode) {
		_leave(" = -ENOMEM");
		return ERR_PTR(-ENOMEM);
	}

	_debug("GOT INODE %p { ino=%lu, vl=%llx, vn=%llx, u=%x }",
	       inode, inode->i_ino, fid.vid, fid.vnode, fid.unique);

	vnode = AFS_FS_I(inode);

	if (inode->i_state & I_NEW) {
		netfs_inode_init(&vnode->netfs, NULL, false);
		simple_inode_init_ts(inode);
		set_nlink(inode, 2);
		inode->i_size		= 0;
		inode->i_mode		= S_IFDIR | 0555;
		inode->i_op		= &afs_autocell_inode_operations;
		inode->i_uid		= GLOBAL_ROOT_UID;
		inode->i_gid		= GLOBAL_ROOT_GID;
		inode->i_blocks		= 0;
		inode->i_generation	= 0;
		inode->i_flags		|= S_AUTOMOUNT | S_NOATIME;

		set_bit(AFS_VNODE_PSEUDODIR, &vnode->flags);
		set_bit(AFS_VNODE_MOUNTPOINT, &vnode->flags);

		unlock_new_inode(inode);
	}
	_leave(" = %p", inode);
	return inode;
}

/*
 * Try to automount the mountpoint with pseudo directory, if the autocell
 * option is set.
 */
static struct dentry *afs_dynroot_lookup_cell(struct inode *dir, struct dentry *dentry,
					      unsigned int flags)
{
	struct afs_cell *cell = NULL;
	struct afs_net *net = afs_d2net(dentry);
	struct inode *inode = NULL;
	const char *name = dentry->d_name.name;
	size_t len = dentry->d_name.len;
	bool dotted = false;
	int ret = -ENOENT;

	/* Names prefixed with a dot are R/W mounts. */
	if (name[0] == '.') {
		name++;
		len--;
		dotted = true;
	}

	cell = afs_lookup_cell(net, name, len, NULL,
			       AFS_LOOKUP_CELL_DYNROOT,
			       afs_cell_trace_use_lookup_dynroot);
	if (IS_ERR(cell)) {
		ret = PTR_ERR(cell);
		goto out_no_cell;
	}

	inode = afs_iget_pseudo_dir(dir->i_sb, cell->dynroot_ino * 2 + dotted);
	if (IS_ERR(inode)) {
		ret = PTR_ERR(inode);
		goto out;
	}

	dentry->d_fsdata = cell;
	return d_splice_alias(inode, dentry);

out:
	afs_unuse_cell(cell, afs_cell_trace_unuse_lookup_dynroot);
out_no_cell:
	if (!inode)
		return d_splice_alias(inode, dentry);
	return ret == -ENOENT ? NULL : ERR_PTR(ret);
}

/*
 * Look up an entry in a dynroot directory.
 */
static struct dentry *afs_dynroot_lookup(struct inode *dir, struct dentry *dentry,
					 unsigned int flags)
{
	_enter("%pd", dentry);

	if (flags & LOOKUP_CREATE)
		return ERR_PTR(-EOPNOTSUPP);

	if (dentry->d_name.len >= AFSNAMEMAX) {
		_leave(" = -ENAMETOOLONG");
		return ERR_PTR(-ENAMETOOLONG);
	}

	if (dentry->d_name.len == 5 &&
	    memcmp(dentry->d_name.name, "@cell", 5) == 0)
		return afs_lookup_atcell(dir, dentry, 2);

	if (dentry->d_name.len == 6 &&
	    memcmp(dentry->d_name.name, ".@cell", 6) == 0)
		return afs_lookup_atcell(dir, dentry, 3);

	return afs_dynroot_lookup_cell(dir, dentry, flags);
}

const struct inode_operations afs_dynroot_inode_operations = {
	.lookup		= afs_dynroot_lookup,
};

static void afs_dynroot_d_release(struct dentry *dentry)
{
	struct afs_cell *cell = dentry->d_fsdata;

	afs_unuse_cell(cell, afs_cell_trace_unuse_dynroot_mntpt);
}

/*
 * Keep @cell symlink dentries around, but only keep cell autodirs when they're
 * being used.
 */
static int afs_dynroot_delete_dentry(const struct dentry *dentry)
{
	const struct qstr *name = &dentry->d_name;

	if (name->len == 5 && memcmp(name->name, "@cell", 5) == 0)
		return 0;
	if (name->len == 6 && memcmp(name->name, ".@cell", 6) == 0)
		return 0;
	return 1;
}

const struct dentry_operations afs_dynroot_dentry_operations = {
	.d_delete	= afs_dynroot_delete_dentry,
	.d_release	= afs_dynroot_d_release,
	.d_automount	= afs_d_automount,
};

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
 * Create an inode for the @cell or .@cell symlinks.
 */
static struct dentry *afs_lookup_atcell(struct inode *dir, struct dentry *dentry, ino_t ino)
{
	struct afs_vnode *vnode;
	struct inode *inode;
	struct afs_fid fid = { .vnode = ino, .unique = 1, };

	inode = iget5_locked(dir->i_sb, fid.vnode,
			     afs_iget5_pseudo_test, afs_iget5_pseudo_set, &fid);
	if (!inode)
		return ERR_PTR(-ENOMEM);

	vnode = AFS_FS_I(inode);

	if (inode->i_state & I_NEW) {
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
	}
	return d_splice_alias(inode, dentry);
}

/*
 * Transcribe the cell database into readdir content under the RCU read lock.
 * Each cell produces two entries, one prefixed with a dot and one not.
 */
static int afs_dynroot_readdir_cells(struct afs_net *net, struct dir_context *ctx)
{
	const struct afs_cell *cell;
	loff_t newpos;

	_enter("%llu", ctx->pos);

	for (;;) {
		unsigned int ix = ctx->pos >> 1;

		cell = idr_get_next(&net->cells_dyn_ino, &ix);
		if (!cell)
			return 0;
		if (READ_ONCE(cell->state) == AFS_CELL_REMOVING ||
		    READ_ONCE(cell->state) == AFS_CELL_DEAD) {
			ctx->pos += 2;
			ctx->pos &= ~1;
			continue;
		}

		newpos = ix << 1;
		if (newpos > ctx->pos)
			ctx->pos = newpos;

		_debug("pos %llu -> cell %u", ctx->pos, cell->dynroot_ino);

		if ((ctx->pos & 1) == 0) {
			if (!dir_emit(ctx, cell->name, cell->name_len,
				      cell->dynroot_ino, DT_DIR))
				return 0;
			ctx->pos++;
		}
		if ((ctx->pos & 1) == 1) {
			if (!dir_emit(ctx, cell->name - 1, cell->name_len + 1,
				      cell->dynroot_ino + 1, DT_DIR))
				return 0;
			ctx->pos++;
		}
	}
	return 0;
}

/*
 * Read the AFS dynamic root directory.  This produces a list of cellnames,
 * dotted and undotted, along with @cell and .@cell links if configured.
 */
static int afs_dynroot_readdir(struct file *file, struct dir_context *ctx)
{
	struct afs_net *net = afs_d2net(file->f_path.dentry);
	int ret = 0;

	if (!dir_emit_dots(file, ctx))
		return 0;

	if (ctx->pos == 2) {
		if (rcu_access_pointer(net->ws_cell) &&
		    !dir_emit(ctx, "@cell", 5, 2, DT_LNK))
			return 0;
		ctx->pos = 3;
	}
	if (ctx->pos == 3) {
		if (rcu_access_pointer(net->ws_cell) &&
		    !dir_emit(ctx, ".@cell", 6, 3, DT_LNK))
			return 0;
		ctx->pos = 4;
	}

	if ((unsigned long long)ctx->pos <= AFS_MAX_DYNROOT_CELL_INO) {
		down_read(&net->cells_lock);
		ret = afs_dynroot_readdir_cells(net, ctx);
		up_read(&net->cells_lock);
	}
	return ret;
}

static const struct file_operations afs_dynroot_file_operations = {
	.llseek		= generic_file_llseek,
	.read		= generic_read_dir,
	.iterate_shared	= afs_dynroot_readdir,
	.fsync		= noop_fsync,
};

/*
 * Create an inode for a dynamic root directory.
 */
struct inode *afs_dynroot_iget_root(struct super_block *sb)
{
	struct afs_super_info *as = AFS_FS_S(sb);
	struct afs_vnode *vnode;
	struct inode *inode;
	struct afs_fid fid = { .vid = 0, .vnode = 1, .unique = 1,};

	if (as->volume)
		fid.vid = as->volume->vid;

	inode = iget5_locked(sb, fid.vnode,
			     afs_iget5_pseudo_test, afs_iget5_pseudo_set, &fid);
	if (!inode)
		return ERR_PTR(-ENOMEM);

	vnode = AFS_FS_I(inode);

	/* there shouldn't be an existing inode */
	if (inode->i_state & I_NEW) {
		netfs_inode_init(&vnode->netfs, NULL, false);
		simple_inode_init_ts(inode);
		set_nlink(inode, 2);
		inode->i_size		= 0;
		inode->i_mode		= S_IFDIR | 0555;
		inode->i_op		= &afs_dynroot_inode_operations;
		inode->i_fop		= &afs_dynroot_file_operations;
		inode->i_uid		= GLOBAL_ROOT_UID;
		inode->i_gid		= GLOBAL_ROOT_GID;
		inode->i_blocks		= 0;
		inode->i_generation	= 0;
		inode->i_flags		|= S_NOATIME;

		set_bit(AFS_VNODE_PSEUDODIR, &vnode->flags);
		unlock_new_inode(inode);
	}
	_leave(" = %p", inode);
	return inode;
}
