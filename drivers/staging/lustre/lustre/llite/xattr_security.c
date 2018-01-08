// SPDX-License-Identifier: GPL-2.0
/*
 * GPL HEADER START
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License version 2 for more details (a copy is included
 * in the LICENSE file that accompanied this code).
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; If not, see http://www.gnu.org/licenses
 *
 * GPL HEADER END
 */

/*
 * Copyright (c) 2014 Bull SAS
 * Author: Sebastien Buisson sebastien.buisson@bull.net
 */

/*
 * lustre/llite/xattr_security.c
 * Handler for storing security labels as extended attributes.
 */

#include <linux/types.h>
#include <linux/security.h>
#include <linux/selinux.h>
#include <linux/xattr.h>
#include "llite_internal.h"

/**
 * A helper function for ll_security_inode_init_security()
 * that takes care of setting xattrs
 *
 * Get security context of @inode from @xattr_array,
 * and put it in 'security.xxx' xattr of dentry
 * stored in @fs_info.
 *
 * \retval 0        success
 * \retval -ENOMEM  if no memory could be allocated for xattr name
 * \retval < 0      failure to set xattr
 */
static int
ll_initxattrs(struct inode *inode, const struct xattr *xattr_array,
	      void *fs_info)
{
	struct dentry *dentry = fs_info;
	const struct xattr *xattr;
	int err = 0;

	for (xattr = xattr_array; xattr->name; xattr++) {
		char *full_name;

		full_name = kasprintf(GFP_KERNEL, "%s%s",
				      XATTR_SECURITY_PREFIX, xattr->name);
		if (!full_name) {
			err = -ENOMEM;
			break;
		}

		err = __vfs_setxattr(dentry, inode, full_name, xattr->value,
				     xattr->value_len, XATTR_CREATE);
		kfree(full_name);
		if (err < 0)
			break;
	}
	return err;
}

/**
 * Initializes security context
 *
 * Get security context of @inode in @dir,
 * and put it in 'security.xxx' xattr of @dentry.
 *
 * \retval 0        success, or SELinux is disabled
 * \retval -ENOMEM  if no memory could be allocated for xattr name
 * \retval < 0      failure to get security context or set xattr
 */
int
ll_init_security(struct dentry *dentry, struct inode *inode, struct inode *dir)
{
	if (!selinux_is_enabled())
		return 0;

	return security_inode_init_security(inode, dir, NULL,
					    &ll_initxattrs, dentry);
}
