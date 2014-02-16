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
#include <linux/string.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include "xattr.h"

static int v9fs_xattr_security_get(struct dentry *dentry, const char *name,
			void *buffer, size_t size, int type)
{
	int retval;
	char *full_name;
	size_t name_len;
	size_t prefix_len = XATTR_SECURITY_PREFIX_LEN;

	if (name == NULL)
		return -EINVAL;

	if (strcmp(name, "") == 0)
		return -EINVAL;

	name_len = strlen(name);
	full_name = kmalloc(prefix_len + name_len + 1 , GFP_KERNEL);
	if (!full_name)
		return -ENOMEM;
	memcpy(full_name, XATTR_SECURITY_PREFIX, prefix_len);
	memcpy(full_name+prefix_len, name, name_len);
	full_name[prefix_len + name_len] = '\0';

	retval = v9fs_xattr_get(dentry, full_name, buffer, size);
	kfree(full_name);
	return retval;
}

static int v9fs_xattr_security_set(struct dentry *dentry, const char *name,
			const void *value, size_t size, int flags, int type)
{
	int retval;
	char *full_name;
	size_t name_len;
	size_t prefix_len = XATTR_SECURITY_PREFIX_LEN;

	if (name == NULL)
		return -EINVAL;

	if (strcmp(name, "") == 0)
		return -EINVAL;

	name_len = strlen(name);
	full_name = kmalloc(prefix_len + name_len + 1 , GFP_KERNEL);
	if (!full_name)
		return -ENOMEM;
	memcpy(full_name, XATTR_SECURITY_PREFIX, prefix_len);
	memcpy(full_name + prefix_len, name, name_len);
	full_name[prefix_len + name_len] = '\0';

	retval = v9fs_xattr_set(dentry, full_name, value, size, flags);
	kfree(full_name);
	return retval;
}

struct xattr_handler v9fs_xattr_security_handler = {
	.prefix	= XATTR_SECURITY_PREFIX,
	.get	= v9fs_xattr_security_get,
	.set	= v9fs_xattr_security_set,
};
