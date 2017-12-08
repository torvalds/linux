/*
 * Copyright (C) 2014-2017 Junjiro R. Okajima
 *
 * This program, aufs is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * posix acl operations
 */

#include <linux/fs.h>
#include "aufs.h"

struct posix_acl *aufs_get_acl(struct inode *inode, int type)
{
	struct posix_acl *acl;
	int err;
	aufs_bindex_t bindex;
	struct inode *h_inode;
	struct super_block *sb;

	acl = NULL;
	sb = inode->i_sb;
	si_read_lock(sb, AuLock_FLUSH);
	ii_read_lock_child(inode);
	if (!(sb->s_flags & MS_POSIXACL))
		goto out;

	bindex = au_ibtop(inode);
	h_inode = au_h_iptr(inode, bindex);
	if (unlikely(!h_inode
		     || ((h_inode->i_mode & S_IFMT)
			 != (inode->i_mode & S_IFMT)))) {
		err = au_busy_or_stale();
		acl = ERR_PTR(err);
		goto out;
	}

	/* always topmost only */
	acl = get_acl(h_inode, type);
	if (!IS_ERR_OR_NULL(acl))
		set_cached_acl(inode, type, acl);

out:
	ii_read_unlock(inode);
	si_read_unlock(sb);

	AuTraceErrPtr(acl);
	return acl;
}

int aufs_set_acl(struct inode *inode, struct posix_acl *acl, int type)
{
	int err;
	ssize_t ssz;
	struct dentry *dentry;
	struct au_sxattr arg = {
		.type = AU_ACL_SET,
		.u.acl_set = {
			.acl	= acl,
			.type	= type
		},
	};

	IMustLock(inode);

	if (inode->i_ino == AUFS_ROOT_INO)
		dentry = dget(inode->i_sb->s_root);
	else {
		dentry = d_find_alias(inode);
		if (!dentry)
			dentry = d_find_any_alias(inode);
		if (!dentry) {
			pr_warn("cannot handle this inode, "
				"please report to aufs-users ML\n");
			err = -ENOENT;
			goto out;
		}
	}

	ssz = au_sxattr(dentry, inode, &arg);
	dput(dentry);
	err = ssz;
	if (ssz >= 0) {
		err = 0;
		set_cached_acl(inode, type, acl);
	}

out:
	return err;
}
