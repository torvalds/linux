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
#include <linux/sched.h>
#include <linux/uio.h>
#include <net/9p/9p.h>
#include <net/9p/client.h>

#include "fid.h"
#include "xattr.h"

ssize_t v9fs_fid_xattr_get(struct p9_fid *fid, const char *name,
			   void *buffer, size_t buffer_size)
{
	ssize_t retval;
	u64 attr_size;
	struct p9_fid *attr_fid;
	struct kvec kvec = {.iov_base = buffer, .iov_len = buffer_size};
	struct iov_iter to;
	int err;

	iov_iter_kvec(&to, READ | ITER_KVEC, &kvec, 1, buffer_size);

	attr_fid = p9_client_xattrwalk(fid, name, &attr_size);
	if (IS_ERR(attr_fid)) {
		retval = PTR_ERR(attr_fid);
		p9_debug(P9_DEBUG_VFS, "p9_client_attrwalk failed %zd\n",
			 retval);
		return retval;
	}
	if (attr_size > buffer_size) {
		if (!buffer_size) /* request to get the attr_size */
			retval = attr_size;
		else
			retval = -ERANGE;
	} else {
		iov_iter_truncate(&to, attr_size);
		retval = p9_client_read(attr_fid, 0, &to, &err);
		if (err)
			retval = err;
	}
	p9_client_clunk(attr_fid);
	return retval;
}


/*
 * v9fs_xattr_get()
 *
 * Copy an extended attribute into the buffer
 * provided, or compute the buffer size required.
 * Buffer is NULL to compute the size of the buffer required.
 *
 * Returns a negative error number on failure, or the number of bytes
 * used / required on success.
 */
ssize_t v9fs_xattr_get(struct dentry *dentry, const char *name,
		       void *buffer, size_t buffer_size)
{
	struct p9_fid *fid;

	p9_debug(P9_DEBUG_VFS, "name = %s value_len = %zu\n",
		 name, buffer_size);
	fid = v9fs_fid_lookup(dentry);
	if (IS_ERR(fid))
		return PTR_ERR(fid);

	return v9fs_fid_xattr_get(fid, name, buffer, buffer_size);
}

/*
 * v9fs_xattr_set()
 *
 * Create, replace or remove an extended attribute for this inode. Buffer
 * is NULL to remove an existing extended attribute, and non-NULL to
 * either replace an existing extended attribute, or create a new extended
 * attribute. The flags XATTR_REPLACE and XATTR_CREATE
 * specify that an extended attribute must exist and must not exist
 * previous to the call, respectively.
 *
 * Returns 0, or a negative error number on failure.
 */
int v9fs_xattr_set(struct dentry *dentry, const char *name,
		   const void *value, size_t value_len, int flags)
{
	struct p9_fid *fid = v9fs_fid_lookup(dentry);
	if (IS_ERR(fid))
		return PTR_ERR(fid);
	return v9fs_fid_xattr_set(fid, name, value, value_len, flags);
}

int v9fs_fid_xattr_set(struct p9_fid *fid, const char *name,
		   const void *value, size_t value_len, int flags)
{
	struct kvec kvec = {.iov_base = (void *)value, .iov_len = value_len};
	struct iov_iter from;
	int retval;

	iov_iter_kvec(&from, WRITE | ITER_KVEC, &kvec, 1, value_len);

	p9_debug(P9_DEBUG_VFS, "name = %s value_len = %zu flags = %d\n",
		 name, value_len, flags);

	/* Clone it */
	fid = p9_client_walk(fid, 0, NULL, 1);
	if (IS_ERR(fid))
		return PTR_ERR(fid);

	/*
	 * On success fid points to xattr
	 */
	retval = p9_client_xattrcreate(fid, name, value_len, flags);
	if (retval < 0)
		p9_debug(P9_DEBUG_VFS, "p9_client_xattrcreate failed %d\n",
			 retval);
	else
		p9_client_write(fid, 0, &from, &retval);
	p9_client_clunk(fid);
	return retval;
}

ssize_t v9fs_listxattr(struct dentry *dentry, char *buffer, size_t buffer_size)
{
	return v9fs_xattr_get(dentry, NULL, buffer, buffer_size);
}

static int v9fs_xattr_handler_get(const struct xattr_handler *handler,
				  struct dentry *dentry, struct inode *inode,
				  const char *name, void *buffer, size_t size)
{
	const char *full_name = xattr_full_name(handler, name);

	return v9fs_xattr_get(dentry, full_name, buffer, size);
}

static int v9fs_xattr_handler_set(const struct xattr_handler *handler,
				  struct dentry *dentry, const char *name,
				  const void *value, size_t size, int flags)
{
	const char *full_name = xattr_full_name(handler, name);

	return v9fs_xattr_set(dentry, full_name, value, size, flags);
}

static struct xattr_handler v9fs_xattr_user_handler = {
	.prefix	= XATTR_USER_PREFIX,
	.get	= v9fs_xattr_handler_get,
	.set	= v9fs_xattr_handler_set,
};

static struct xattr_handler v9fs_xattr_trusted_handler = {
	.prefix	= XATTR_TRUSTED_PREFIX,
	.get	= v9fs_xattr_handler_get,
	.set	= v9fs_xattr_handler_set,
};

#ifdef CONFIG_9P_FS_SECURITY
static struct xattr_handler v9fs_xattr_security_handler = {
	.prefix	= XATTR_SECURITY_PREFIX,
	.get	= v9fs_xattr_handler_get,
	.set	= v9fs_xattr_handler_set,
};
#endif

const struct xattr_handler *v9fs_xattr_handlers[] = {
	&v9fs_xattr_user_handler,
	&v9fs_xattr_trusted_handler,
#ifdef CONFIG_9P_FS_POSIX_ACL
	&v9fs_xattr_acl_access_handler,
	&v9fs_xattr_acl_default_handler,
#endif
#ifdef CONFIG_9P_FS_SECURITY
	&v9fs_xattr_security_handler,
#endif
	NULL
};
