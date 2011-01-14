/*
 * Copyright IBM Corporation, 2010
 * Author Aneesh Kumar K.V <aneesh.kumar@linux.vnet.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2.1 of the GNU Lesser General Public License
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <net/9p/9p.h>
#include <net/9p/client.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/posix_acl_xattr.h>
#include "xattr.h"
#include "acl.h"
#include "v9fs_vfs.h"
#include "v9fs.h"

static struct posix_acl *__v9fs_get_acl(struct p9_fid *fid, char *name)
{
	ssize_t size;
	void *value = NULL;
	struct posix_acl *acl = NULL;;

	size = v9fs_fid_xattr_get(fid, name, NULL, 0);
	if (size > 0) {
		value = kzalloc(size, GFP_NOFS);
		if (!value)
			return ERR_PTR(-ENOMEM);
		size = v9fs_fid_xattr_get(fid, name, value, size);
		if (size > 0) {
			acl = posix_acl_from_xattr(value, size);
			if (IS_ERR(acl))
				goto err_out;
		}
	} else if (size == -ENODATA || size == 0 ||
		   size == -ENOSYS || size == -EOPNOTSUPP) {
		acl = NULL;
	} else
		acl = ERR_PTR(-EIO);

err_out:
	kfree(value);
	return acl;
}

int v9fs_get_acl(struct inode *inode, struct p9_fid *fid)
{
	int retval = 0;
	struct posix_acl *pacl, *dacl;
	struct v9fs_session_info *v9ses;

	v9ses = v9fs_inode2v9ses(inode);
	if ((v9ses->flags & V9FS_ACCESS_MASK) != V9FS_ACCESS_CLIENT) {
		set_cached_acl(inode, ACL_TYPE_DEFAULT, NULL);
		set_cached_acl(inode, ACL_TYPE_ACCESS, NULL);
		return 0;
	}
	/* get the default/access acl values and cache them */
	dacl = __v9fs_get_acl(fid, POSIX_ACL_XATTR_DEFAULT);
	pacl = __v9fs_get_acl(fid, POSIX_ACL_XATTR_ACCESS);

	if (!IS_ERR(dacl) && !IS_ERR(pacl)) {
		set_cached_acl(inode, ACL_TYPE_DEFAULT, dacl);
		set_cached_acl(inode, ACL_TYPE_ACCESS, pacl);
		posix_acl_release(dacl);
		posix_acl_release(pacl);
	} else
		retval = -EIO;

	return retval;
}

static struct posix_acl *v9fs_get_cached_acl(struct inode *inode, int type)
{
	struct posix_acl *acl;
	/*
	 * 9p Always cache the acl value when
	 * instantiating the inode (v9fs_inode_from_fid)
	 */
	acl = get_cached_acl(inode, type);
	BUG_ON(acl == ACL_NOT_CACHED);
	return acl;
}

int v9fs_check_acl(struct inode *inode, int mask, unsigned int flags)
{
	struct posix_acl *acl;
	struct v9fs_session_info *v9ses;

	if (flags & IPERM_FLAG_RCU)
		return -ECHILD;

	v9ses = v9fs_inode2v9ses(inode);
	if ((v9ses->flags & V9FS_ACCESS_MASK) != V9FS_ACCESS_CLIENT) {
		/*
		 * On access = client mode get the acl
		 * values from the server
		 */
		return 0;
	}
	acl = v9fs_get_cached_acl(inode, ACL_TYPE_ACCESS);

	if (IS_ERR(acl))
		return PTR_ERR(acl);
	if (acl) {
		int error = posix_acl_permission(inode, acl, mask);
		posix_acl_release(acl);
		return error;
	}
	return -EAGAIN;
}

static int v9fs_set_acl(struct dentry *dentry, int type, struct posix_acl *acl)
{
	int retval;
	char *name;
	size_t size;
	void *buffer;
	struct inode *inode = dentry->d_inode;

	set_cached_acl(inode, type, acl);
	/* Set a setxattr request to server */
	size = posix_acl_xattr_size(acl->a_count);
	buffer = kmalloc(size, GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;
	retval = posix_acl_to_xattr(acl, buffer, size);
	if (retval < 0)
		goto err_free_out;
	switch (type) {
	case ACL_TYPE_ACCESS:
		name = POSIX_ACL_XATTR_ACCESS;
		break;
	case ACL_TYPE_DEFAULT:
		name = POSIX_ACL_XATTR_DEFAULT;
		break;
	default:
		BUG();
	}
	retval = v9fs_xattr_set(dentry, name, buffer, size, 0);
err_free_out:
	kfree(buffer);
	return retval;
}

int v9fs_acl_chmod(struct dentry *dentry)
{
	int retval = 0;
	struct posix_acl *acl, *clone;
	struct inode *inode = dentry->d_inode;

	if (S_ISLNK(inode->i_mode))
		return -EOPNOTSUPP;
	acl = v9fs_get_cached_acl(inode, ACL_TYPE_ACCESS);
	if (acl) {
		clone = posix_acl_clone(acl, GFP_KERNEL);
		posix_acl_release(acl);
		if (!clone)
			return -ENOMEM;
		retval = posix_acl_chmod_masq(clone, inode->i_mode);
		if (!retval)
			retval = v9fs_set_acl(dentry, ACL_TYPE_ACCESS, clone);
		posix_acl_release(clone);
	}
	return retval;
}

int v9fs_set_create_acl(struct dentry *dentry,
			struct posix_acl *dpacl, struct posix_acl *pacl)
{
	if (dpacl)
		v9fs_set_acl(dentry, ACL_TYPE_DEFAULT, dpacl);
	if (pacl)
		v9fs_set_acl(dentry, ACL_TYPE_ACCESS, pacl);
	posix_acl_release(dpacl);
	posix_acl_release(pacl);
	return 0;
}

int v9fs_acl_mode(struct inode *dir, mode_t *modep,
		  struct posix_acl **dpacl, struct posix_acl **pacl)
{
	int retval = 0;
	mode_t mode = *modep;
	struct posix_acl *acl = NULL;

	if (!S_ISLNK(mode)) {
		acl = v9fs_get_cached_acl(dir, ACL_TYPE_DEFAULT);
		if (IS_ERR(acl))
			return PTR_ERR(acl);
		if (!acl)
			mode &= ~current_umask();
	}
	if (acl) {
		struct posix_acl *clone;

		if (S_ISDIR(mode))
			*dpacl = acl;
		clone = posix_acl_clone(acl, GFP_NOFS);
		retval = -ENOMEM;
		if (!clone)
			goto cleanup;

		retval = posix_acl_create_masq(clone, &mode);
		if (retval < 0) {
			posix_acl_release(clone);
			goto cleanup;
		}
		if (retval > 0)
			*pacl = clone;
	}
	*modep  = mode;
	return 0;
cleanup:
	posix_acl_release(acl);
	return retval;

}

static int v9fs_remote_get_acl(struct dentry *dentry, const char *name,
			       void *buffer, size_t size, int type)
{
	char *full_name;

	switch (type) {
	case ACL_TYPE_ACCESS:
		full_name =  POSIX_ACL_XATTR_ACCESS;
		break;
	case ACL_TYPE_DEFAULT:
		full_name = POSIX_ACL_XATTR_DEFAULT;
		break;
	default:
		BUG();
	}
	return v9fs_xattr_get(dentry, full_name, buffer, size);
}

static int v9fs_xattr_get_acl(struct dentry *dentry, const char *name,
			      void *buffer, size_t size, int type)
{
	struct v9fs_session_info *v9ses;
	struct posix_acl *acl;
	int error;

	if (strcmp(name, "") != 0)
		return -EINVAL;

	v9ses = v9fs_inode2v9ses(dentry->d_inode);
	/*
	 * We allow set/get/list of acl when access=client is not specified
	 */
	if ((v9ses->flags & V9FS_ACCESS_MASK) != V9FS_ACCESS_CLIENT)
		return v9fs_remote_get_acl(dentry, name, buffer, size, type);

	acl = v9fs_get_cached_acl(dentry->d_inode, type);
	if (IS_ERR(acl))
		return PTR_ERR(acl);
	if (acl == NULL)
		return -ENODATA;
	error = posix_acl_to_xattr(acl, buffer, size);
	posix_acl_release(acl);

	return error;
}

static int v9fs_remote_set_acl(struct dentry *dentry, const char *name,
			      const void *value, size_t size,
			      int flags, int type)
{
	char *full_name;

	switch (type) {
	case ACL_TYPE_ACCESS:
		full_name =  POSIX_ACL_XATTR_ACCESS;
		break;
	case ACL_TYPE_DEFAULT:
		full_name = POSIX_ACL_XATTR_DEFAULT;
		break;
	default:
		BUG();
	}
	return v9fs_xattr_set(dentry, full_name, value, size, flags);
}


static int v9fs_xattr_set_acl(struct dentry *dentry, const char *name,
			      const void *value, size_t size,
			      int flags, int type)
{
	int retval;
	struct posix_acl *acl;
	struct v9fs_session_info *v9ses;
	struct inode *inode = dentry->d_inode;

	if (strcmp(name, "") != 0)
		return -EINVAL;

	v9ses = v9fs_inode2v9ses(dentry->d_inode);
	/*
	 * set the attribute on the remote. Without even looking at the
	 * xattr value. We leave it to the server to validate
	 */
	if ((v9ses->flags & V9FS_ACCESS_MASK) != V9FS_ACCESS_CLIENT)
		return v9fs_remote_set_acl(dentry, name,
					   value, size, flags, type);

	if (S_ISLNK(inode->i_mode))
		return -EOPNOTSUPP;
	if (!is_owner_or_cap(inode))
		return -EPERM;
	if (value) {
		/* update the cached acl value */
		acl = posix_acl_from_xattr(value, size);
		if (IS_ERR(acl))
			return PTR_ERR(acl);
		else if (acl) {
			retval = posix_acl_valid(acl);
			if (retval)
				goto err_out;
		}
	} else
		acl = NULL;

	switch (type) {
	case ACL_TYPE_ACCESS:
		name = POSIX_ACL_XATTR_ACCESS;
		if (acl) {
			mode_t mode = inode->i_mode;
			retval = posix_acl_equiv_mode(acl, &mode);
			if (retval < 0)
				goto err_out;
			else {
				struct iattr iattr;
				if (retval == 0) {
					/*
					 * ACL can be represented
					 * by the mode bits. So don't
					 * update ACL.
					 */
					acl = NULL;
					value = NULL;
					size = 0;
				}
				/* Updte the mode bits */
				iattr.ia_mode = ((mode & S_IALLUGO) |
						 (inode->i_mode & ~S_IALLUGO));
				iattr.ia_valid = ATTR_MODE;
				/* FIXME should we update ctime ?
				 * What is the following setxattr update the
				 * mode ?
				 */
				v9fs_vfs_setattr_dotl(dentry, &iattr);
			}
		}
		break;
	case ACL_TYPE_DEFAULT:
		name = POSIX_ACL_XATTR_DEFAULT;
		if (!S_ISDIR(inode->i_mode)) {
			retval = -EINVAL;
			goto err_out;
		}
		break;
	default:
		BUG();
	}
	retval = v9fs_xattr_set(dentry, name, value, size, flags);
	if (!retval)
		set_cached_acl(inode, type, acl);
err_out:
	posix_acl_release(acl);
	return retval;
}

const struct xattr_handler v9fs_xattr_acl_access_handler = {
	.prefix	= POSIX_ACL_XATTR_ACCESS,
	.flags	= ACL_TYPE_ACCESS,
	.get	= v9fs_xattr_get_acl,
	.set	= v9fs_xattr_set_acl,
};

const struct xattr_handler v9fs_xattr_acl_default_handler = {
	.prefix	= POSIX_ACL_XATTR_DEFAULT,
	.flags	= ACL_TYPE_DEFAULT,
	.get	= v9fs_xattr_get_acl,
	.set	= v9fs_xattr_set_acl,
};
