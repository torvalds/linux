// SPDX-License-Identifier: GPL-2.0-or-later
/* Extended attribute handling for AFS.  We use xattrs to get and set metadata
 * instead of providing pioctl().
 *
 * Copyright (C) 2017 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/xattr.h>
#include "internal.h"

/*
 * Deal with the result of a successful fetch ACL operation.
 */
static void afs_acl_success(struct afs_operation *op)
{
	afs_vnode_commit_status(op, &op->file[0]);
}

static void afs_acl_put(struct afs_operation *op)
{
	kfree(op->acl);
}

static const struct afs_operation_ops afs_fetch_acl_operation = {
	.issue_afs_rpc	= afs_fs_fetch_acl,
	.success	= afs_acl_success,
	.put		= afs_acl_put,
};

/*
 * Get a file's ACL.
 */
static int afs_xattr_get_acl(const struct xattr_handler *handler,
			     struct dentry *dentry,
			     struct inode *inode, const char *name,
			     void *buffer, size_t size)
{
	struct afs_operation *op;
	struct afs_vnode *vnode = AFS_FS_I(inode);
	struct afs_acl *acl = NULL;
	int ret;

	op = afs_alloc_operation(NULL, vnode->volume);
	if (IS_ERR(op))
		return -ENOMEM;

	afs_op_set_vnode(op, 0, vnode);
	op->ops = &afs_fetch_acl_operation;

	afs_begin_vnode_operation(op);
	afs_wait_for_operation(op);
	acl = op->acl;
	op->acl = NULL;
	ret = afs_put_operation(op);

	if (ret == 0) {
		ret = acl->size;
		if (size > 0) {
			if (acl->size <= size)
				memcpy(buffer, acl->data, acl->size);
			else
				ret = -ERANGE;
		}
	}

	kfree(acl);
	return ret;
}

static bool afs_make_acl(struct afs_operation *op,
			 const void *buffer, size_t size)
{
	struct afs_acl *acl;

	acl = kmalloc(struct_size(acl, data, size), GFP_KERNEL);
	if (!acl) {
		afs_op_nomem(op);
		return false;
	}

	acl->size = size;
	memcpy(acl->data, buffer, size);
	op->acl = acl;
	return true;
}

static const struct afs_operation_ops afs_store_acl_operation = {
	.issue_afs_rpc	= afs_fs_store_acl,
	.success	= afs_acl_success,
	.put		= afs_acl_put,
};

/*
 * Set a file's AFS3 ACL.
 */
static int afs_xattr_set_acl(const struct xattr_handler *handler,
			     struct mnt_idmap *idmap,
                             struct dentry *dentry,
                             struct inode *inode, const char *name,
                             const void *buffer, size_t size, int flags)
{
	struct afs_operation *op;
	struct afs_vnode *vnode = AFS_FS_I(inode);

	if (flags == XATTR_CREATE)
		return -EINVAL;

	op = afs_alloc_operation(NULL, vnode->volume);
	if (IS_ERR(op))
		return -ENOMEM;

	afs_op_set_vnode(op, 0, vnode);
	if (!afs_make_acl(op, buffer, size))
		return afs_put_operation(op);

	op->ops = &afs_store_acl_operation;
	return afs_do_sync_operation(op);
}

static const struct xattr_handler afs_xattr_afs_acl_handler = {
	.name   = "afs.acl",
	.get    = afs_xattr_get_acl,
	.set    = afs_xattr_set_acl,
};

static const struct afs_operation_ops yfs_fetch_opaque_acl_operation = {
	.issue_yfs_rpc	= yfs_fs_fetch_opaque_acl,
	.success	= afs_acl_success,
	/* Don't free op->yacl in .put here */
};

/*
 * Get a file's YFS ACL.
 */
static int afs_xattr_get_yfs(const struct xattr_handler *handler,
			     struct dentry *dentry,
			     struct inode *inode, const char *name,
			     void *buffer, size_t size)
{
	struct afs_operation *op;
	struct afs_vnode *vnode = AFS_FS_I(inode);
	struct yfs_acl *yacl = NULL;
	char buf[16], *data;
	int which = 0, dsize, ret = -ENOMEM;

	if (strcmp(name, "acl") == 0)
		which = 0;
	else if (strcmp(name, "acl_inherited") == 0)
		which = 1;
	else if (strcmp(name, "acl_num_cleaned") == 0)
		which = 2;
	else if (strcmp(name, "vol_acl") == 0)
		which = 3;
	else
		return -EOPNOTSUPP;

	yacl = kzalloc(sizeof(struct yfs_acl), GFP_KERNEL);
	if (!yacl)
		goto error;

	if (which == 0)
		yacl->flags |= YFS_ACL_WANT_ACL;
	else if (which == 3)
		yacl->flags |= YFS_ACL_WANT_VOL_ACL;

	op = afs_alloc_operation(NULL, vnode->volume);
	if (IS_ERR(op))
		goto error_yacl;

	afs_op_set_vnode(op, 0, vnode);
	op->yacl = yacl;
	op->ops = &yfs_fetch_opaque_acl_operation;

	afs_begin_vnode_operation(op);
	afs_wait_for_operation(op);
	ret = afs_put_operation(op);

	if (ret == 0) {
		switch (which) {
		case 0:
			data = yacl->acl->data;
			dsize = yacl->acl->size;
			break;
		case 1:
			data = buf;
			dsize = scnprintf(buf, sizeof(buf), "%u", yacl->inherit_flag);
			break;
		case 2:
			data = buf;
			dsize = scnprintf(buf, sizeof(buf), "%u", yacl->num_cleaned);
			break;
		case 3:
			data = yacl->vol_acl->data;
			dsize = yacl->vol_acl->size;
			break;
		default:
			ret = -EOPNOTSUPP;
			goto error_yacl;
		}

		ret = dsize;
		if (size > 0) {
			if (dsize <= size)
				memcpy(buffer, data, dsize);
			else
				ret = -ERANGE;
		}
	} else if (ret == -ENOTSUPP) {
		ret = -ENODATA;
	}

error_yacl:
	yfs_free_opaque_acl(yacl);
error:
	return ret;
}

static const struct afs_operation_ops yfs_store_opaque_acl2_operation = {
	.issue_yfs_rpc	= yfs_fs_store_opaque_acl2,
	.success	= afs_acl_success,
	.put		= afs_acl_put,
};

/*
 * Set a file's YFS ACL.
 */
static int afs_xattr_set_yfs(const struct xattr_handler *handler,
			     struct mnt_idmap *idmap,
                             struct dentry *dentry,
                             struct inode *inode, const char *name,
                             const void *buffer, size_t size, int flags)
{
	struct afs_operation *op;
	struct afs_vnode *vnode = AFS_FS_I(inode);
	int ret;

	if (flags == XATTR_CREATE ||
	    strcmp(name, "acl") != 0)
		return -EINVAL;

	op = afs_alloc_operation(NULL, vnode->volume);
	if (IS_ERR(op))
		return -ENOMEM;

	afs_op_set_vnode(op, 0, vnode);
	if (!afs_make_acl(op, buffer, size))
		return afs_put_operation(op);

	op->ops = &yfs_store_opaque_acl2_operation;
	ret = afs_do_sync_operation(op);
	if (ret == -ENOTSUPP)
		ret = -ENODATA;
	return ret;
}

static const struct xattr_handler afs_xattr_yfs_handler = {
	.prefix	= "afs.yfs.",
	.get	= afs_xattr_get_yfs,
	.set	= afs_xattr_set_yfs,
};

/*
 * Get the name of the cell on which a file resides.
 */
static int afs_xattr_get_cell(const struct xattr_handler *handler,
			      struct dentry *dentry,
			      struct inode *inode, const char *name,
			      void *buffer, size_t size)
{
	struct afs_vnode *vnode = AFS_FS_I(inode);
	struct afs_cell *cell = vnode->volume->cell;
	size_t namelen;

	namelen = cell->name_len;
	if (size == 0)
		return namelen;
	if (namelen > size)
		return -ERANGE;
	memcpy(buffer, cell->name, namelen);
	return namelen;
}

static const struct xattr_handler afs_xattr_afs_cell_handler = {
	.name	= "afs.cell",
	.get	= afs_xattr_get_cell,
};

/*
 * Get the volume ID, vnode ID and vnode uniquifier of a file as a sequence of
 * hex numbers separated by colons.
 */
static int afs_xattr_get_fid(const struct xattr_handler *handler,
			     struct dentry *dentry,
			     struct inode *inode, const char *name,
			     void *buffer, size_t size)
{
	struct afs_vnode *vnode = AFS_FS_I(inode);
	char text[16 + 1 + 24 + 1 + 8 + 1];
	size_t len;

	/* The volume ID is 64-bit, the vnode ID is 96-bit and the
	 * uniquifier is 32-bit.
	 */
	len = scnprintf(text, sizeof(text), "%llx:", vnode->fid.vid);
	if (vnode->fid.vnode_hi)
		len += scnprintf(text + len, sizeof(text) - len, "%x%016llx",
				vnode->fid.vnode_hi, vnode->fid.vnode);
	else
		len += scnprintf(text + len, sizeof(text) - len, "%llx",
				 vnode->fid.vnode);
	len += scnprintf(text + len, sizeof(text) - len, ":%x",
			 vnode->fid.unique);

	if (size == 0)
		return len;
	if (len > size)
		return -ERANGE;
	memcpy(buffer, text, len);
	return len;
}

static const struct xattr_handler afs_xattr_afs_fid_handler = {
	.name	= "afs.fid",
	.get	= afs_xattr_get_fid,
};

/*
 * Get the name of the volume on which a file resides.
 */
static int afs_xattr_get_volume(const struct xattr_handler *handler,
			      struct dentry *dentry,
			      struct inode *inode, const char *name,
			      void *buffer, size_t size)
{
	struct afs_vnode *vnode = AFS_FS_I(inode);
	const char *volname = vnode->volume->name;
	size_t namelen;

	namelen = strlen(volname);
	if (size == 0)
		return namelen;
	if (namelen > size)
		return -ERANGE;
	memcpy(buffer, volname, namelen);
	return namelen;
}

static const struct xattr_handler afs_xattr_afs_volume_handler = {
	.name	= "afs.volume",
	.get	= afs_xattr_get_volume,
};

const struct xattr_handler * const afs_xattr_handlers[] = {
	&afs_xattr_afs_acl_handler,
	&afs_xattr_afs_cell_handler,
	&afs_xattr_afs_fid_handler,
	&afs_xattr_afs_volume_handler,
	&afs_xattr_yfs_handler,		/* afs.yfs. prefix */
	NULL
};
