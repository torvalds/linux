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
#ifndef FS_9P_ACL_H
#define FS_9P_ACL_H

#ifdef CONFIG_9P_FS_POSIX_ACL
extern int v9fs_get_acl(struct inode *, struct p9_fid *);
extern int v9fs_check_acl(struct inode *inode, int mask);
extern int v9fs_acl_chmod(struct dentry *);
extern int v9fs_set_create_acl(struct dentry *,
			       struct posix_acl *, struct posix_acl *);
extern int v9fs_acl_mode(struct inode *dir, mode_t *modep,
			 struct posix_acl **dpacl, struct posix_acl **pacl);
#else
#define v9fs_check_acl NULL
static inline int v9fs_get_acl(struct inode *inode, struct p9_fid *fid)
{
	return 0;
}
static inline int v9fs_acl_chmod(struct dentry *dentry)
{
	return 0;
}
static inline int v9fs_set_create_acl(struct dentry *dentry,
				      struct posix_acl *dpacl,
				      struct posix_acl *pacl)
{
	return 0;
}
static inline int v9fs_acl_mode(struct inode *dir, mode_t *modep,
				struct posix_acl **dpacl,
				struct posix_acl **pacl)
{
	return 0;
}

#endif
#endif /* FS_9P_XATTR_H */
