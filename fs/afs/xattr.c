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
	struct afs_status_cb *scb;
	struct afs_vnode *vnode = AFS_FS_I(inode);
	struct afs_acl *acl = NULL;
	struct key *key;
	int ret = -ENOMEM;

	scb = kzalloc(sizeof(struct afs_status_cb), GFP_NOFS);
	if (!scb)
		goto error;

	key = afs_request_key(vnode->volume->cell);
	if (IS_ERR(key)) {
		ret = PTR_ERR(key);
		goto error_scb;
	}

	ret = -ERESTARTSYS;
	if (afs_begin_vnode_operation(&fc, vnode, key, true)) {
		afs_dataversion_t data_version = vnode->status.data_version;

		while (afs_select_fileserver(&fc)) {
			fc.cb_break = afs_calc_vnode_cb_break(vnode);
			acl = afs_fs_fetch_acl(&fc, scb);
		}

		afs_check_for_remote_deletion(&fc, fc.vnode);
		afs_vnode_commit_status(&fc, vnode, fc.cb_break,
					&data_version, scb);
		ret = afs_end_vnode_operation(&fc);
	}

	if (ret == 0) {
		ret = acl->size;
		if (size > 0) {
			if (acl->size <= size)
				memcpy(buffer, acl->data, acl->size);
			else
				ret = -ERANGE;
		}
		kfree(acl);
	}

	key_put(key);
error_scb:
	kfree(scb);
error:
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
	struct afs_status_cb *scb;
	struct afs_vnode *vnode = AFS_FS_I(inode);
	struct afs_acl *acl = NULL;
	struct key *key;
	int ret = -ENOMEM;

	if (flags == XATTR_CREATE)
		return -EINVAL;

	scb = kzalloc(sizeof(struct afs_status_cb), GFP_NOFS);
	if (!scb)
		goto error;

	acl = kmalloc(sizeof(*acl) + size, GFP_KERNEL);
	if (!acl)
		goto error_scb;

	key = afs_request_key(vnode->volume->cell);
	if (IS_ERR(key)) {
		ret = PTR_ERR(key);
		goto error_acl;
	}

	acl->size = size;
	memcpy(acl->data, buffer, size);

	ret = -ERESTARTSYS;
	if (afs_begin_vnode_operation(&fc, vnode, key, true)) {
		afs_dataversion_t data_version = vnode->status.data_version;

		while (afs_select_fileserver(&fc)) {
			fc.cb_break = afs_calc_vnode_cb_break(vnode);
			afs_fs_store_acl(&fc, acl, scb);
		}

		afs_check_for_remote_deletion(&fc, fc.vnode);
		afs_vnode_commit_status(&fc, vnode, fc.cb_break,
					&data_version, scb);
		ret = afs_end_vnode_operation(&fc);
	}

	key_put(key);
error_acl:
	kfree(acl);
error_scb:
	kfree(scb);
error:
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
	struct afs_status_cb *scb;
	struct afs_vnode *vnode = AFS_FS_I(inode);
	struct yfs_acl *yacl = NULL;
	struct key *key;
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

	scb = kzalloc(sizeof(struct afs_status_cb), GFP_NOFS);
	if (!scb)
		goto error_yacl;

	key = afs_request_key(vnode->volume->cell);
	if (IS_ERR(key)) {
		ret = PTR_ERR(key);
		goto error_scb;
	}

	ret = -ERESTARTSYS;
	if (afs_begin_vnode_operation(&fc, vnode, key, true)) {
		afs_dataversion_t data_version = vnode->status.data_version;

		while (afs_select_fileserver(&fc)) {
			fc.cb_break = afs_calc_vnode_cb_break(vnode);
			yfs_fs_fetch_opaque_acl(&fc, yacl, scb);
		}

		afs_check_for_remote_deletion(&fc, fc.vnode);
		afs_vnode_commit_status(&fc, vnode, fc.cb_break,
					&data_version, scb);
		ret = afs_end_vnode_operation(&fc);
	}

	if (ret < 0)
		goto error_key;

	switch (which) {
	case 0:
		data = yacl->acl->data;
		dsize = yacl->acl->size;
		break;
	case 1:
		data = buf;
		dsize = snprintf(buf, sizeof(buf), "%u", yacl->inherit_flag);
		break;
	case 2:
		data = buf;
		dsize = snprintf(buf, sizeof(buf), "%u", yacl->num_cleaned);
		break;
	case 3:
		data = yacl->vol_acl->data;
		dsize = yacl->vol_acl->size;
		break;
	default:
		ret = -EOPNOTSUPP;
		goto error_key;
	}

	ret = dsize;
	if (size > 0) {
		if (dsize > size) {
			ret = -ERANGE;
			goto error_key;
		}
		memcpy(buffer, data, dsize);
	}

error_key:
	key_put(key);
error_scb:
	kfree(scb);
error_yacl:
	yfs_free_opaque_acl(yacl);
error:
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
	struct afs_status_cb *scb;
	struct afs_vnode *vnode = AFS_FS_I(inode);
	struct afs_acl *acl = NULL;
	struct key *key;
	int ret = -ENOMEM;

	if (flags == XATTR_CREATE ||
	    strcmp(name, "acl") != 0)
		return -EINVAL;

	scb = kzalloc(sizeof(struct afs_status_cb), GFP_NOFS);
	if (!scb)
		goto error;

	acl = kmalloc(sizeof(*acl) + size, GFP_KERNEL);
	if (!acl)
		goto error_scb;

	acl->size = size;
	memcpy(acl->data, buffer, size);

	key = afs_request_key(vnode->volume->cell);
	if (IS_ERR(key)) {
		ret = PTR_ERR(key);
		goto error_acl;
	}

	ret = -ERESTARTSYS;
	if (afs_begin_vnode_operation(&fc, vnode, key, true)) {
		afs_dataversion_t data_version = vnode->status.data_version;

		while (afs_select_fileserver(&fc)) {
			fc.cb_break = afs_calc_vnode_cb_break(vnode);
			yfs_fs_store_opaque_acl2(&fc, acl, scb);
		}

		afs_check_for_remote_deletion(&fc, fc.vnode);
		afs_vnode_commit_status(&fc, vnode, fc.cb_break,
					&data_version, scb);
		ret = afs_end_vnode_operation(&fc);
	}

error_acl:
	kfree(acl);
	key_put(key);
error_scb:
	kfree(scb);
error:
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
