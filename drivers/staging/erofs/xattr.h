/* SPDX-License-Identifier: GPL-2.0
 *
 * linux/drivers/staging/erofs/xattr.h
 *
 * Copyright (C) 2017-2018 HUAWEI, Inc.
 *             http://www.huawei.com/
 * Created by Gao Xiang <gaoxiang25@huawei.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of the Linux
 * distribution for more details.
 */
#ifndef __EROFS_XATTR_H
#define __EROFS_XATTR_H

#include "internal.h"
#include <linux/posix_acl_xattr.h>
#include <linux/xattr.h>

/* Attribute not found */
#define ENOATTR         ENODATA

static inline unsigned inlinexattr_header_size(struct inode *inode)
{
	return sizeof(struct erofs_xattr_ibody_header)
		+ sizeof(u32) * EROFS_V(inode)->xattr_shared_count;
}

static inline erofs_blk_t
xattrblock_addr(struct erofs_sb_info *sbi, unsigned xattr_id)
{
#ifdef CONFIG_EROFS_FS_XATTR
	return sbi->xattr_blkaddr +
		xattr_id * sizeof(__u32) / EROFS_BLKSIZ;
#else
	return 0;
#endif
}

static inline unsigned
xattrblock_offset(struct erofs_sb_info *sbi, unsigned xattr_id)
{
	return (xattr_id * sizeof(__u32)) % EROFS_BLKSIZ;
}

extern const struct xattr_handler erofs_xattr_user_handler;
extern const struct xattr_handler erofs_xattr_trusted_handler;
#ifdef CONFIG_EROFS_FS_SECURITY
extern const struct xattr_handler erofs_xattr_security_handler;
#endif

static inline const struct xattr_handler *erofs_xattr_handler(unsigned index)
{
static const struct xattr_handler *xattr_handler_map[] = {
	[EROFS_XATTR_INDEX_USER] = &erofs_xattr_user_handler,
#ifdef CONFIG_EROFS_FS_POSIX_ACL
	[EROFS_XATTR_INDEX_POSIX_ACL_ACCESS] = &posix_acl_access_xattr_handler,
	[EROFS_XATTR_INDEX_POSIX_ACL_DEFAULT] =
		&posix_acl_default_xattr_handler,
#endif
	[EROFS_XATTR_INDEX_TRUSTED] = &erofs_xattr_trusted_handler,
#ifdef CONFIG_EROFS_FS_SECURITY
	[EROFS_XATTR_INDEX_SECURITY] = &erofs_xattr_security_handler,
#endif
};
	return index && index < ARRAY_SIZE(xattr_handler_map) ?
		xattr_handler_map[index] : NULL;
}

#ifdef CONFIG_EROFS_FS_XATTR
extern const struct xattr_handler *erofs_xattr_handlers[];

int erofs_getxattr(struct inode *, int, const char *, void *, size_t);
ssize_t erofs_listxattr(struct dentry *, char *, size_t);
#else
static int __maybe_unused erofs_getxattr(struct inode *inode, int index,
	const char *name,
	void *buffer, size_t buffer_size)
{
	return -ENOTSUPP;
}

static ssize_t __maybe_unused erofs_listxattr(struct dentry *dentry,
	char *buffer, size_t buffer_size)
{
	return -ENOTSUPP;
}
#endif

#ifdef CONFIG_EROFS_FS_POSIX_ACL
struct posix_acl *erofs_get_acl(struct inode *inode, int type);
#else
#define erofs_get_acl	(NULL)
#endif

#endif

