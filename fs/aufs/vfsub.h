/*
 * Copyright (C) 2005-2017 Junjiro R. Okajima
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
 * sub-routines for VFS
 */

#ifndef __AUFS_VFSUB_H__
#define __AUFS_VFSUB_H__

#ifdef __KERNEL__

#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/posix_acl.h>
#include <linux/xattr.h>
#include "debug.h"

/* copied from linux/fs/internal.h */
/* todo: BAD approach!! */
extern void __mnt_drop_write(struct vfsmount *);
extern int open_check_o_direct(struct file *f);

/* ---------------------------------------------------------------------- */

/* lock subclass for lower inode */
/* default MAX_LOCKDEP_SUBCLASSES(8) is not enough */
/* reduce? gave up. */
enum {
	AuLsc_I_Begin = I_MUTEX_PARENT2, /* 5 */
	AuLsc_I_PARENT,		/* lower inode, parent first */
	AuLsc_I_PARENT2,	/* copyup dirs */
	AuLsc_I_PARENT3,	/* copyup wh */
	AuLsc_I_CHILD,
	AuLsc_I_CHILD2,
	AuLsc_I_End
};

/* to debug easier, do not make them inlined functions */
#define MtxMustLock(mtx)	AuDebugOn(!mutex_is_locked(mtx))
#define IMustLock(i)		AuDebugOn(!inode_is_locked(i))

/* why VFS doesn't define it? */
static inline
void vfsub_inode_lock_shared_nested(struct inode *inode, unsigned int sc)
{
	down_read_nested(&inode->i_rwsem, sc);
}

/* ---------------------------------------------------------------------- */

static inline void vfsub_drop_nlink(struct inode *inode)
{
	AuDebugOn(!inode->i_nlink);
	drop_nlink(inode);
}

static inline void vfsub_dead_dir(struct inode *inode)
{
	AuDebugOn(!S_ISDIR(inode->i_mode));
	inode->i_flags |= S_DEAD;
	clear_nlink(inode);
}

static inline int vfsub_native_ro(struct inode *inode)
{
	return sb_rdonly(inode->i_sb)
		|| IS_RDONLY(inode)
		/* || IS_APPEND(inode) */
		|| IS_IMMUTABLE(inode);
}

#ifdef CONFIG_AUFS_BR_FUSE
int vfsub_test_mntns(struct vfsmount *mnt, struct super_block *h_sb);
#else
AuStubInt0(vfsub_test_mntns, struct vfsmount *mnt, struct super_block *h_sb);
#endif

int vfsub_sync_filesystem(struct super_block *h_sb, int wait);

/* ---------------------------------------------------------------------- */

int vfsub_update_h_iattr(struct path *h_path, int *did);
struct file *vfsub_dentry_open(struct path *path, int flags);
struct file *vfsub_filp_open(const char *path, int oflags, int mode);
struct vfsub_aopen_args {
	struct file	*file;
	unsigned int	open_flag;
	umode_t		create_mode;
	int		*opened;
};
struct au_branch;
int vfsub_atomic_open(struct inode *dir, struct dentry *dentry,
		      struct vfsub_aopen_args *args, struct au_branch *br);
int vfsub_kern_path(const char *name, unsigned int flags, struct path *path);

struct dentry *vfsub_lookup_one_len_unlocked(const char *name,
					     struct dentry *parent, int len);
struct dentry *vfsub_lookup_one_len(const char *name, struct dentry *parent,
				    int len);

struct vfsub_lkup_one_args {
	struct dentry **errp;
	struct qstr *name;
	struct dentry *parent;
};

static inline struct dentry *vfsub_lkup_one(struct qstr *name,
					    struct dentry *parent)
{
	return vfsub_lookup_one_len(name->name, parent, name->len);
}

void vfsub_call_lkup_one(void *args);

/* ---------------------------------------------------------------------- */

static inline int vfsub_mnt_want_write(struct vfsmount *mnt)
{
	int err;

	lockdep_off();
	err = mnt_want_write(mnt);
	lockdep_on();
	return err;
}

static inline void vfsub_mnt_drop_write(struct vfsmount *mnt)
{
	lockdep_off();
	mnt_drop_write(mnt);
	lockdep_on();
}

#if 0 /* reserved */
static inline void vfsub_mnt_drop_write_file(struct file *file)
{
	lockdep_off();
	mnt_drop_write_file(file);
	lockdep_on();
}
#endif

/* ---------------------------------------------------------------------- */

struct au_hinode;
struct dentry *vfsub_lock_rename(struct dentry *d1, struct au_hinode *hdir1,
				 struct dentry *d2, struct au_hinode *hdir2);
void vfsub_unlock_rename(struct dentry *d1, struct au_hinode *hdir1,
			 struct dentry *d2, struct au_hinode *hdir2);

int vfsub_create(struct inode *dir, struct path *path, int mode,
		 bool want_excl);
int vfsub_symlink(struct inode *dir, struct path *path,
		  const char *symname);
int vfsub_mknod(struct inode *dir, struct path *path, int mode, dev_t dev);
int vfsub_link(struct dentry *src_dentry, struct inode *dir,
	       struct path *path, struct inode **delegated_inode);
int vfsub_rename(struct inode *src_hdir, struct dentry *src_dentry,
		 struct inode *hdir, struct path *path,
		 struct inode **delegated_inode, unsigned int flags);
int vfsub_mkdir(struct inode *dir, struct path *path, int mode);
int vfsub_rmdir(struct inode *dir, struct path *path);

/* ---------------------------------------------------------------------- */

ssize_t vfsub_read_u(struct file *file, char __user *ubuf, size_t count,
		     loff_t *ppos);
ssize_t vfsub_read_k(struct file *file, void *kbuf, size_t count,
			loff_t *ppos);
ssize_t vfsub_write_u(struct file *file, const char __user *ubuf, size_t count,
		      loff_t *ppos);
ssize_t vfsub_write_k(struct file *file, void *kbuf, size_t count,
		      loff_t *ppos);
int vfsub_flush(struct file *file, fl_owner_t id);
int vfsub_iterate_dir(struct file *file, struct dir_context *ctx);

static inline loff_t vfsub_f_size_read(struct file *file)
{
	return i_size_read(file_inode(file));
}

static inline unsigned int vfsub_file_flags(struct file *file)
{
	unsigned int flags;

	spin_lock(&file->f_lock);
	flags = file->f_flags;
	spin_unlock(&file->f_lock);

	return flags;
}

static inline int vfsub_file_execed(struct file *file)
{
	/* todo: direct access f_flags */
	return !!(vfsub_file_flags(file) & __FMODE_EXEC);
}

#if 0 /* reserved */
static inline void vfsub_file_accessed(struct file *h_file)
{
	file_accessed(h_file);
	vfsub_update_h_iattr(&h_file->f_path, /*did*/NULL); /*ignore*/
}
#endif

#if 0 /* reserved */
static inline void vfsub_touch_atime(struct vfsmount *h_mnt,
				     struct dentry *h_dentry)
{
	struct path h_path = {
		.dentry	= h_dentry,
		.mnt	= h_mnt
	};
	touch_atime(&h_path);
	vfsub_update_h_iattr(&h_path, /*did*/NULL); /*ignore*/
}
#endif

static inline int vfsub_update_time(struct inode *h_inode, struct timespec *ts,
				    int flags)
{
	return update_time(h_inode, ts, flags);
	/* no vfsub_update_h_iattr() since we don't have struct path */
}

#ifdef CONFIG_FS_POSIX_ACL
static inline int vfsub_acl_chmod(struct inode *h_inode, umode_t h_mode)
{
	int err;

	err = posix_acl_chmod(h_inode, h_mode);
	if (err == -EOPNOTSUPP)
		err = 0;
	return err;
}
#else
AuStubInt0(vfsub_acl_chmod, struct inode *h_inode, umode_t h_mode);
#endif

long vfsub_splice_to(struct file *in, loff_t *ppos,
		     struct pipe_inode_info *pipe, size_t len,
		     unsigned int flags);
long vfsub_splice_from(struct pipe_inode_info *pipe, struct file *out,
		       loff_t *ppos, size_t len, unsigned int flags);

static inline long vfsub_truncate(struct path *path, loff_t length)
{
	long err;

	lockdep_off();
	err = vfs_truncate(path, length);
	lockdep_on();
	return err;
}

int vfsub_trunc(struct path *h_path, loff_t length, unsigned int attr,
		struct file *h_file);
int vfsub_fsync(struct file *file, struct path *path, int datasync);

/*
 * re-use branch fs's ioctl(FICLONE) while aufs itself doesn't support such
 * ioctl.
 */
static inline int vfsub_clone_file_range(struct file *src, struct file *dst,
					 u64 len)
{
	int err;

	lockdep_off();
	err = vfs_clone_file_range(src, 0, dst, 0, len);
	lockdep_on();

	return err;
}

/* copy_file_range(2) is a systemcall */
static inline ssize_t vfsub_copy_file_range(struct file *src, loff_t src_pos,
					    struct file *dst, loff_t dst_pos,
					    size_t len, unsigned int flags)
{
	ssize_t ssz;

	lockdep_off();
	ssz = vfs_copy_file_range(src, src_pos, dst, dst_pos, len, flags);
	lockdep_on();

	return ssz;
}

/* ---------------------------------------------------------------------- */

static inline loff_t vfsub_llseek(struct file *file, loff_t offset, int origin)
{
	loff_t err;

	lockdep_off();
	err = vfs_llseek(file, offset, origin);
	lockdep_on();
	return err;
}

/* ---------------------------------------------------------------------- */

int vfsub_sio_mkdir(struct inode *dir, struct path *path, int mode);
int vfsub_sio_rmdir(struct inode *dir, struct path *path);
int vfsub_sio_notify_change(struct path *path, struct iattr *ia,
			    struct inode **delegated_inode);
int vfsub_notify_change(struct path *path, struct iattr *ia,
			struct inode **delegated_inode);
int vfsub_unlink(struct inode *dir, struct path *path,
		 struct inode **delegated_inode, int force);

static inline int vfsub_getattr(const struct path *path, struct kstat *st)
{
	return vfs_getattr(path, st, STATX_BASIC_STATS, AT_STATX_SYNC_AS_STAT);
}

/* ---------------------------------------------------------------------- */

static inline int vfsub_setxattr(struct dentry *dentry, const char *name,
				 const void *value, size_t size, int flags)
{
	int err;

	lockdep_off();
	err = vfs_setxattr(dentry, name, value, size, flags);
	lockdep_on();

	return err;
}

static inline int vfsub_removexattr(struct dentry *dentry, const char *name)
{
	int err;

	lockdep_off();
	err = vfs_removexattr(dentry, name);
	lockdep_on();

	return err;
}

#endif /* __KERNEL__ */
#endif /* __AUFS_VFSUB_H__ */
