/* Extended attribute handling for AFS.  We use xattrs to get and set metadata
 * instead of providing pioctl().
 *
 * Copyright (C) 2017 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/xattr.h>
#include "internal.h"

static const char afs_xattr_list[] =
	"afs.acl\0"
	"afs.cell\0"
	"afs.fid\0"
	"afs.volume\0"
	"afs.yfs.acl\0"
	"afs.yfs.acl_inherited\0"
	"afs.yfs.acl_num_cleaned\0"
	"afs.yfs.vol_acl";

/*
 * Retrieve a list of the supported xattrs.
 */
ssize_t afs_listxattr(struct dentry *dentry, char *buffer, size_t size)
{
	if (size == 0)
		return sizeof(afs_xattr_list);
	if (size < sizeof(afs_xattr_list))
		return -ERANGE;
	memcpy(buffer, afs_xattr_list, sizeof(afs_xattr_list));
	return sizeof(afs_xattr_list);
}

/*
 * Get a file's ACL.
 */
static int afs_xattr_get_acl(const struct xattr_handler *handler,
			     struct dentry *dentry,
			     struct inode *inode, const char *name,
			     void *buffer, size_t size)
{
	struct afs_fs_cursor fc;
	struct afs_vnode *vnode = AFS_FS_I(inode);
	struct afs_acl *acl = NULL;
	struct key *key;
	int ret;

	key = afs_request_key(vnode->volume->cell);
	if (IS_ERR(key))
		return PTR_ERR(key);

	ret = -ERESTARTSYS;
	if (afs_begin_vnode_operation(&fc, vnode, key)) {
		while (afs_select_fileserver(&fc)) {
			fc.cb_break = afs_calc_vnode_cb_break(vnode);
			acl = afs_fs_fetch_acl(&fc);
		}

		afs_check_for_remote_deletion(&fc, fc.vnode);
		afs_vnode_commit_status(&fc, vnode, fc.cb_break);
		ret = afs_end_vnode_operation(&fc);
	}

	if (ret == 0) {
		ret = acl->size;
		if (size > 0) {
			ret = -ERANGE;
			if (acl->size > size)
				return -ERANGE;
			memcpy(buffer, acl->data, acl->size);
			ret = acl->size;
		}
		kfree(acl);
	}

	key_put(key);
	return ret;
}

/*
 * Set a file's AFS3 ACL.
 */
static int afs_xattr_set_acl(const struct xattr_handler *handler,
                             struct dentry *dentry,
                             struct inode *inode, const char *name,
                             const void *buffer, size_t size, int flags)
{
	struct afs_fs_cursor fc;
	struct afs_vnode *vnode = AFS_FS_I(inode);
	struct afs_acl *acl = NULL;
	struct key *key;
	int ret;

	if (flags == XATTR_CREATE)
		return -EINVAL;

	key = afs_request_key(vnode->volume->cell);
	if (IS_ERR(key))
		return PTR_ERR(key);

	acl = kmalloc(sizeof(*acl) + size, GFP_KERNEL);
	if (!acl) {
		key_put(key);
		return -ENOMEM;
	}

	acl->size = size;
	memcpy(acl->data, buffer, size);

	ret = -ERESTARTSYS;
	if (afs_begin_vnode_operation(&fc, vnode, key)) {
		while (afs_select_fileserver(&fc)) {
			fc.cb_break = afs_calc_vnode_cb_break(vnode);
			afs_fs_store_acl(&fc, acl);
		}

		afs_check_for_remote_deletion(&fc, fc.vnode);
		afs_vnode_commit_status(&fc, vnode, fc.cb_break);
		ret = afs_end_vnode_operation(&fc);
	}

	kfree(acl);
	key_put(key);
	return ret;
}

static const struct xattr_handler afs_xattr_afs_acl_handler = {
	.name   = "afs.acl",
	.get    = afs_xattr_get_acl,
	.set    = afs_xattr_set_acl,
};

/*
 * Get a file's YFS ACL.
 */
static int afs_xattr_get_yfs(const struct xattr_handler *handler,
			     struct dentry *dentry,
			     struct inode *inode, const char *name,
			     void *buffer, size_t size)
{
	struct afs_fs_cursor fc;
	struct afs_vnode *vnode = AFS_FS_I(inode);
	struct yfs_acl *yacl = NULL;
	struct key *key;
	unsigned int flags = 0;
	char buf[16], *data;
	int which = 0, dsize, ret;

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

	if (which == 0)
		flags |= YFS_ACL_WANT_ACL;
	else if (which == 3)
		flags |= YFS_ACL_WANT_VOL_ACL;

	key = afs_request_key(vnode->volume->cell);
	if (IS_ERR(key))
		return PTR_ERR(key);

	ret = -ERESTARTSYS;
	if (afs_begin_vnode_operation(&fc, vnode, key)) {
		while (afs_select_fileserver(&fc)) {
			fc.cb_break = afs_calc_vnode_cb_break(vnode);
			yacl = yfs_fs_fetch_opaque_acl(&fc, flags);
		}

		afs_check_for_remote_deletion(&fc, fc.vnode);
		afs_vnode_commit_status(&fc, vnode, fc.cb_break);
		ret = afs_end_vnode_operation(&fc);
	}

	if (ret == 0) {
		switch (which) {
		case 0:
			data = yacl->acl->data;
			dsize = yacl->acl->size;
			break;
		case 1:
			data = buf;
			dsize = snprintf(buf, sizeof(buf), "%u",
					 yacl->inherit_flag);
			break;
		case 2:
			data = buf;
			dsize = snprintf(buf, sizeof(buf), "%u",
					 yacl->num_cleaned);
			break;
		case 3:
			data = yacl->vol_acl->data;
			dsize = yacl->vol_acl->size;
			break;
		default:
			ret = -EOPNOTSUPP;
			goto out;
		}

		ret = dsize;
		if (size > 0) {
			if (dsize > size) {
				ret = -ERANGE;
				goto out;
			}
			memcpy(buffer, data, dsize);
		}
	}

out:
	yfs_free_opaque_acl(yacl);
	key_put(key);
	return ret;
}

/*
 * Set a file's YFS ACL.
 */
static int afs_xattr_set_yfs(const struct xattr_handler *handler,
                             struct dentry *dentry,
                             struct inode *inode, const char *name,
                             const void *buffer, size_t size, int flags)
{
	struct afs_fs_cursor fc;
	struct afs_vnode *vnode = AFS_FS_I(inode);
	struct afs_acl *acl = NULL;
	struct key *key;
	int ret;

	if (flags == XATTR_CREATE ||
	    strcmp(name, "acl") != 0)
		return -EINVAL;

	key = afs_request_key(vnode->volume->cell);
	if (IS_ERR(key))
		return PTR_ERR(key);

	acl = kmalloc(sizeof(*acl) + size, GFP_KERNEL);
	if (!acl) {
		key_put(key);
		return -ENOMEM;
	}

	acl->size = size;
	memcpy(acl->data, buffer, size);

	ret = -ERESTARTSYS;
	if (afs_begin_vnode_operation(&fc, vnode, key)) {
		while (afs_select_fileserver(&fc)) {
			fc.cb_break = afs_calc_vnode_cb_break(vnode);
			yfs_fs_store_opaque_acl2(&fc, acl);
		}

		afs_check_for_remote_deletion(&fc, fc.vnode);
		afs_vnode_commit_status(&fc, vnode, fc.cb_break);
		ret = afs_end_vnode_operation(&fc);
	}

	kfree(acl);
	key_put(key);
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
	len = sprintf(text, "%llx:", vnode->fid.vid);
	if (vnode->fid.vnode_hi)
		len += sprintf(text + len, "%x%016llx",
			       vnode->fid.vnode_hi, vnode->fid.vnode);
	else
		len += sprintf(text + len, "%llx", vnode->fid.vnode);
	len += sprintf(text + len, ":%x", vnode->fid.unique);

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

const struct xattr_handler *afs_xattr_handlers[] = {
	&afs_xattr_afs_acl_handler,
	&afs_xattr_afs_cell_handler,
	&afs_xattr_afs_fid_handler,
	&afs_xattr_afs_volume_handler,
	&afs_xattr_yfs_handler,		/* afs.yfs. prefix */
	NULL
};
