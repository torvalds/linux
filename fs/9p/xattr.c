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
#include <net/9p/9p.h>
#include <net/9p/client.h>

#include "fid.h"
#include "xattr.h"

ssize_t v9fs_fid_xattr_get(struct p9_fid *fid, const char *name,
			   void *buffer, size_t buffer_size)
{
	ssize_t retval;
	int msize, read_count;
	u64 offset = 0, attr_size;
	struct p9_fid *attr_fid;

	attr_fid = p9_client_xattrwalk(fid, name, &attr_size);
	if (IS_ERR(attr_fid)) {
		retval = PTR_ERR(attr_fid);
		p9_debug(P9_DEBUG_VFS, "p9_client_attrwalk failed %zd\n",
			 retval);
		attr_fid = NULL;
		goto error;
	}
	if (!buffer_size) {
		/* request to get the attr_size */
		retval = attr_size;
		goto error;
	}
	if (attr_size > buffer_size) {
		retval = -ERANGE;
		goto error;
	}
	msize = attr_fid->clnt->msize;
	while (attr_size) {
		if (attr_size > (msize - P9_IOHDRSZ))
			read_count = msize - P9_IOHDRSZ;
		else
			read_count = attr_size;
		read_count = p9_client_read(attr_fid, ((char *)buffer)+offset,
					NULL, offset, read_count);
		if (read_count < 0) {
			/* error in xattr read */
			retval = read_count;
			goto error;
		}
		offset += read_count;
		attr_size -= read_count;
	}
	/* Total read xattr bytes */
	retval = offset;
error:
	if (attr_fid)
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
	u64 offset = 0;
	int retval, msize, write_count;

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
	if (retval < 0) {
		p9_debug(P9_DEBUG_VFS, "p9_client_xattrcreate failed %d\n",
			 retval);
		p9_client_clunk(fid);
		return retval;
	}
	msize = fid->clnt->msize;
	while (value_len) {
		if (value_len > (msize - P9_IOHDRSZ))
			write_count = msize - P9_IOHDRSZ;
		else
			write_count = value_len;
		write_count = p9_client_write(fid, ((char *)value)+offset,
					NULL, offset, write_count);
		if (write_count < 0) {
			/* error in xattr write */
			retval = write_count;
			break;
		}
		offset += write_count;
		value_len -= write_count;
	}
	return p9_client_clunk(fid);
}

ssize_t v9fs_listxattr(struct dentry *dentry, char *buffer, size_t buffer_size)
{
	return v9fs_xattr_get(dentry, NULL, buffer, buffer_size);
}

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
