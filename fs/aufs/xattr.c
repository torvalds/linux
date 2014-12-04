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

static int au_xattr_ignore(int err, char *name, unsigned int ignore_flags)
{
	if (!ignore_flags)
		goto out;
	switch (err) {
	case -ENOMEM:
	case -EDQUOT:
		goto out;
	}

	if ((ignore_flags & AuBrAttr_ICEX) == AuBrAttr_ICEX) {
		err = 0;
		goto out;
	}

#define cmp(brattr, prefix) do {					\
		if (!strncmp(name, XATTR_##prefix##_PREFIX,		\
			     XATTR_##prefix##_PREFIX_LEN)) {		\
			if (ignore_flags & AuBrAttr_ICEX_##brattr)	\
				err = 0;				\
			goto out;					\
		}							\
	} while (0)

	cmp(SEC, SECURITY);
	cmp(SYS, SYSTEM);
	cmp(TR, TRUSTED);
	cmp(USR, USER);
#undef cmp

	if (ignore_flags & AuBrAttr_ICEX_OTH)
		err = 0;

out:
	return err;
}

static const int au_xattr_out_of_list = AuBrAttr_ICEX_OTH << 1;

static int au_do_cpup_xattr(struct dentry *h_dst, struct dentry *h_src,
			    char *name, char **buf, unsigned int ignore_flags)
{
	int err;
	ssize_t ssz;
	struct inode *h_idst;

	ssz = vfs_getxattr_alloc(h_src, name, buf, 0, GFP_NOFS);
	err = ssz;
	if (unlikely(err <= 0)) {
		AuTraceErr(err);
		if (err == -ENODATA
		    || (err == -EOPNOTSUPP
			&& (ignore_flags & au_xattr_out_of_list)))
			err = 0;
		goto out;
	}

	/* unlock it temporary */
	h_idst = h_dst->d_inode;
	mutex_unlock(&h_idst->i_mutex);
	err = vfsub_setxattr(h_dst, name, *buf, ssz, /*flags*/0);
	mutex_lock_nested(&h_idst->i_mutex, AuLsc_I_CHILD2);
	if (unlikely(err)) {
		AuDbg("%s, err %d\n", name, err);
		err = au_xattr_ignore(err, name, ignore_flags);
	}

out:
	return err;
}

int au_cpup_xattr(struct dentry *h_dst, struct dentry *h_src, int ignore_flags)
{
	int err, unlocked, acl_access, acl_default;
	ssize_t ssz;
	struct inode *h_isrc, *h_idst;
	char *value, *p, *o, *e;

	/* try stopping to update the source inode while we are referencing */
	/* there should not be the parent-child relation ship between them */
	h_isrc = h_src->d_inode;
	h_idst = h_dst->d_inode;
	mutex_unlock(&h_idst->i_mutex);
	mutex_lock_nested(&h_isrc->i_mutex, AuLsc_I_CHILD);
	mutex_lock_nested(&h_idst->i_mutex, AuLsc_I_CHILD2);
	unlocked = 0;

	/* some filesystems don't list POSIX ACL, for example tmpfs */
	ssz = vfs_listxattr(h_src, NULL, 0);
	err = ssz;
	if (unlikely(err < 0)) {
		if (err == -ENODATA
		    || err == -EOPNOTSUPP)
			err = 0;	/* ignore */
		goto out;
	}

	err = 0;
	p = NULL;
	o = NULL;
	if (ssz) {
		err = -ENOMEM;
		p = kmalloc(ssz, GFP_NOFS);
		o = p;
		if (unlikely(!p))
			goto out;
		err = vfs_listxattr(h_src, p, ssz);
	}
	mutex_unlock(&h_isrc->i_mutex);
	unlocked = 1;
	AuDbg("err %d, ssz %zd\n", err, ssz);
	if (unlikely(err < 0))
		goto out_free;

	err = 0;
	e = p + ssz;
	value = NULL;
	acl_access = 0;
	acl_default = 0;
	while (!err && p < e) {
		acl_access |= !strncmp(p, XATTR_NAME_POSIX_ACL_ACCESS,
				       sizeof(XATTR_NAME_POSIX_ACL_ACCESS) - 1);
		acl_default |= !strncmp(p, XATTR_NAME_POSIX_ACL_DEFAULT,
					sizeof(XATTR_NAME_POSIX_ACL_DEFAULT)
					- 1);
		err = au_do_cpup_xattr(h_dst, h_src, p, &value, ignore_flags);
		p += strlen(p) + 1;
	}
	AuTraceErr(err);
	ignore_flags |= au_xattr_out_of_list;
	if (!err && !acl_access) {
		err = au_do_cpup_xattr(h_dst, h_src,
				       XATTR_NAME_POSIX_ACL_ACCESS, &value,
				       ignore_flags);
		AuTraceErr(err);
	}
	if (!err && !acl_default) {
		err = au_do_cpup_xattr(h_dst, h_src,
				       XATTR_NAME_POSIX_ACL_DEFAULT, &value,
				       ignore_flags);
		AuTraceErr(err);
	}

	kfree(value);

out_free:
	kfree(o);
out:
	if (!unlocked)
		mutex_unlock(&h_isrc->i_mutex);
	AuTraceErr(err);
	return err;
}

/* ---------------------------------------------------------------------- */

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

/* cf fs/aufs/i_op.c:aufs_setattr() */
static int au_h_path_to_set_attr(struct dentry *dentry,
				 struct au_icpup_args *a, struct path *h_path)
{
	int err;
	struct super_block *sb;

	sb = dentry->d_sb;
	a->udba = au_opt_udba(sb);
	/* no d_unlinked(), to set UDBA_NONE for root */
	if (d_unhashed(dentry))
		a->udba = AuOpt_UDBA_NONE;
	if (a->udba != AuOpt_UDBA_NONE) {
		AuDebugOn(IS_ROOT(dentry));
		err = au_reval_for_attr(dentry, au_sigen(sb));
		if (unlikely(err))
			goto out;
	}
	err = au_pin_and_icpup(dentry, /*ia*/NULL, a);
	if (unlikely(err < 0))
		goto out;

	h_path->dentry = a->h_path.dentry;
	h_path->mnt = au_sbr_mnt(sb, a->btgt);

out:
	return err;
}

enum {
	AU_XATTR_SET,
	AU_XATTR_REMOVE
};

struct au_srxattr {
	int type;
	union {
		struct {
			const char	*name;
			const void	*value;
			size_t		size;
			int		flags;
		} set;
		struct {
			const char	*name;
		} remove;
	} u;
};

static ssize_t au_srxattr(struct dentry *dentry, struct au_srxattr *arg)
{
	int err;
	struct path h_path;
	struct super_block *sb;
	struct au_icpup_args *a;
	struct inode *inode;

	inode = dentry->d_inode;
	IMustLock(inode);

	err = -ENOMEM;
	a = kzalloc(sizeof(*a), GFP_NOFS);
	if (unlikely(!a))
		goto out;

	sb = dentry->d_sb;
	err = si_read_lock(sb, AuLock_FLUSH | AuLock_NOPLM);
	if (unlikely(err))
		goto out_kfree;

	h_path.dentry = NULL;	/* silence gcc */
	di_write_lock_child(dentry);
	err = au_h_path_to_set_attr(dentry, a, &h_path);
	if (unlikely(err))
		goto out_di;

	mutex_unlock(&a->h_inode->i_mutex);
	switch (arg->type) {
	case AU_XATTR_SET:
		err = vfsub_setxattr(h_path.dentry,
				     arg->u.set.name, arg->u.set.value,
				     arg->u.set.size, arg->u.set.flags);
		break;
	case AU_XATTR_REMOVE:
		err = vfsub_removexattr(h_path.dentry, arg->u.remove.name);
		break;
	}
	if (!err)
		au_cpup_attr_timesizes(inode);

	au_unpin(&a->pin);
	if (unlikely(err))
		au_update_dbstart(dentry);

out_di:
	di_write_unlock(dentry);
	si_read_unlock(sb);
out_kfree:
	kfree(a);
out:
	AuTraceErr(err);
	return err;
}

int aufs_setxattr(struct dentry *dentry, const char *name, const void *value,
		  size_t size, int flags)
{
	struct au_srxattr arg = {
		.type = AU_XATTR_SET,
		.u.set = {
			.name	= name,
			.value	= value,
			.size	= size,
			.flags	= flags
		},
	};

	return au_srxattr(dentry, &arg);
}

int aufs_removexattr(struct dentry *dentry, const char *name)
{
	struct au_srxattr arg = {
		.type = AU_XATTR_REMOVE,
		.u.remove = {
			.name	= name
		},
	};

	return au_srxattr(dentry, &arg);
}

/* ---------------------------------------------------------------------- */

#if 0
static size_t au_xattr_list(struct dentry *dentry, char *list, size_t list_size,
			    const char *name, size_t name_len, int type)
{
	return aufs_listxattr(dentry, list, list_size);
}

static int au_xattr_get(struct dentry *dentry, const char *name, void *buffer,
			size_t size, int type)
{
	return aufs_getxattr(dentry, name, buffer, size);
}

static int au_xattr_set(struct dentry *dentry, const char *name,
			const void *value, size_t size, int flags, int type)
{
	return aufs_setxattr(dentry, name, value, size, flags);
}

static const struct xattr_handler au_xattr_handler = {
	/* no prefix, no flags */
	.list	= au_xattr_list,
	.get	= au_xattr_get,
	.set	= au_xattr_set
	/* why no remove? */
};

static const struct xattr_handler *au_xattr_handlers[] = {
	&au_xattr_handler
};

void au_xattr_init(struct super_block *sb)
{
	/* sb->s_xattr = au_xattr_handlers; */
}
#endif
