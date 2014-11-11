/*
 * Copyright (C) 2014 Junjiro R. Okajima
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
 * handling xattr functions
 */

#include <linux/xattr.h>
#include "aufs.h"

enum {
	AU_XATTR_LIST,
	AU_XATTR_GET
};

struct au_lgxattr {
	int type;
	union {
		struct {
			char	*list;
			size_t	size;
		} list;
		struct {
			const char	*name;
			void		*value;
			size_t		size;
		} get;
	} u;
};

static ssize_t au_lgxattr(struct dentry *dentry, struct au_lgxattr *arg)
{
	ssize_t err;
	struct path h_path;
	struct super_block *sb;

	sb = dentry->d_sb;
	err = si_read_lock(sb, AuLock_FLUSH | AuLock_NOPLM);
	if (unlikely(err))
		goto out;
	err = au_h_path_getattr(dentry, /*force*/1, &h_path);
	if (unlikely(err))
		goto out_si;
	if (unlikely(!h_path.dentry))
		/* illegally overlapped or something */
		goto out_di; /* pretending success */

	/* always topmost entry only */
	switch (arg->type) {
	case AU_XATTR_LIST:
		err = vfs_listxattr(h_path.dentry,
				    arg->u.list.list, arg->u.list.size);
		break;
	case AU_XATTR_GET:
		err = vfs_getxattr(h_path.dentry,
				   arg->u.get.name, arg->u.get.value,
				   arg->u.get.size);
		break;
	}

out_di:
	di_read_unlock(dentry, AuLock_IR);
out_si:
	si_read_unlock(sb);
out:
	AuTraceErr(err);
	return err;
}

ssize_t aufs_listxattr(struct dentry *dentry, char *list, size_t size)
{
	struct au_lgxattr arg = {
		.type = AU_XATTR_LIST,
		.u.list = {
			.list	= list,
			.size	= size
		},
	};

	return au_lgxattr(dentry, &arg);
}

ssize_t aufs_getxattr(struct dentry *dentry, const char *name, void *value,
		      size_t size)
{
	struct au_lgxattr arg = {
		.type = AU_XATTR_GET,
		.u.get = {
			.name	= name,
			.value	= value,
			.size	= size
		},
	};

	return au_lgxattr(dentry, &arg);
}
