/*
 * Copyright (C) 2005-2013 Junjiro R. Okajima
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

/*
 * sub-routines for VFS
 */

#include <linux/ima.h>
#include <linux/namei.h>
#include <linux/security.h>
#include <linux/splice.h>
#include "aufs.h"

int vfsub_update_h_iattr(struct path *h_path, int *did)
{
	int err;
	struct kstat st;
	struct super_block *h_sb;

	/* for remote fs, leave work for its getattr or d_revalidate */
	/* for bad i_attr fs, handle them in aufs_getattr() */
	/* still some fs may acquire i_mutex. we need to skip them */
	err = 0;
	if (!did)
		did = &err;
	h_sb = h_path->dentry->d_sb;
	*did = (!au_test_fs_remote(h_sb) && au_test_fs_refresh_iattr(h_sb));
	if (*did)
		err = vfs_getattr(h_path->mnt, h_path->dentry, &st);

	return err;
}

/* ---------------------------------------------------------------------- */

struct file *vfsub_dentry_open(struct path *path, int flags)
{
	struct file *file;

	path_get(path);
	file = dentry_open(path->dentry, path->mnt,
			   flags /* | __FMODE_NONOTIFY */,
			   current_cred());
	if (!IS_ERR_OR_NULL(file)
	    && (file->f_mode & (FMODE_READ | FMODE_WRITE)) == FMODE_READ)
		i_readcount_inc(path->dentry->d_inode);

	return file;
}

struct file *vfsub_filp_open(const char *path, int oflags, int mode)
{
	struct file *file;

	lockdep_off();
	file = filp_open(path,
			 oflags /* | __FMODE_NONOTIFY */,
			 mode);
	lockdep_on();
	if (IS_ERR(file))
		goto out;
	vfsub_update_h_iattr(&file->f_path, /*did*/NULL); /*ignore*/

out:
	return file;
}

int vfsub_kern_path(const char *name, unsigned int flags, struct path *path)
{
	int err;

	err = kern_path(name, flags, path);
	if (!err && path->dentry->d_inode)
		vfsub_update_h_iattr(path, /*did*/NULL); /*ignore*/
	return err;
}

struct dentry *vfsub_lookup_one_len(const char *name, struct dentry *parent,
				    int len)
{
	struct path path = {
		.mnt = NULL
	};

	/* VFS checks it too, but by WARN_ON_ONCE() */
	IMustLock(parent->d_inode);

	path.dentry = lookup_one_len(name, parent, len);
	if (IS_ERR(path.dentry))
		goto out;
	if (path.dentry->d_inode)
		vfsub_update_h_iattr(&path, /*did*/NULL); /*ignore*/

out:
	AuTraceErrPtr(path.dentry);
	return path.dentry;
}

struct dentry *vfsub_lookup_hash(struct nameidata *nd)
{
	struct path path = {
		.mnt = nd->path.mnt
	};

	IMustLock(nd->path.dentry->d_inode);

	path.dentry = lookup_hash(nd);
	if (IS_ERR(path.dentry))
		goto out;
	if (path.dentry->d_inode)
		vfsub_update_h_iattr(&path, /*did*/NULL); /*ignore*/

out:
	AuTraceErrPtr(path.dentry);
	return path.dentry;
}

/*
 * this is "VFS:__lookup_one_len()" which was removed and merged into
 * VFS:lookup_one_len() by the commit.
 *	6a96ba5 2011-03-14 kill __lookup_one_len()
 * this function should always be equivalent to the corresponding part in
 * VFS:lookup_one_len().
 */
int vfsub_name_hash(const char *name, struct qstr *this, int len)
{
	unsigned long hash;
	unsigned int c;

	this->name = name;
	this->len = len;
	if (!len)
		return -EACCES;

	hash = init_name_hash();
	while (len--) {
		c = *(const unsigned char *)name++;
		if (c == '/' || c == '\0')
			return -EACCES;
		hash = partial_name_hash(c, hash);
	}
	this->hash = end_name_hash(hash);
	return 0;
}

/* ---------------------------------------------------------------------- */

struct dentry *vfsub_lock_rename(struct dentry *d1, struct au_hinode *hdir1,
				 struct dentry *d2, struct au_hinode *hdir2)
{
	struct dentry *d;

	lockdep_off();
	d = lock_rename(d1, d2);
	lockdep_on();
	au_hn_suspend(hdir1);
	if (hdir1 != hdir2)
		au_hn_suspend(hdir2);

	return d;
}

void vfsub_unlock_rename(struct dentry *d1, struct au_hinode *hdir1,
			 struct dentry *d2, struct au_hinode *hdir2)
{
	au_hn_resume(hdir1);
	if (hdir1 != hdir2)
		au_hn_resume(hdir2);
	lockdep_off();
	unlock_rename(d1, d2);
	lockdep_on();
}

/* ---------------------------------------------------------------------- */

int vfsub_create(struct inode *dir, struct path *path, int mode)
{
	int err;
	struct dentry *d;

	IMustLock(dir);

	d = path->dentry;
	path->dentry = d->d_parent;
	err = security_path_mknod(path, d, mode, 0);
	path->dentry = d;
	if (unlikely(err))
		goto out;

	if (au_test_fs_null_nd(dir->i_sb))
		err = vfs_create(dir, path->dentry, mode, NULL);
	else {
		struct nameidata h_nd;

		memset(&h_nd, 0, sizeof(h_nd));
		h_nd.flags = LOOKUP_CREATE;
		h_nd.intent.open.flags = O_CREAT
			| vfsub_fmode_to_uint(FMODE_READ);
		h_nd.intent.open.create_mode = mode;
		h_nd.path.dentry = path->dentry->d_parent;
		h_nd.path.mnt = path->mnt;
		path_get(&h_nd.path);
		err = vfs_create(dir, path->dentry, mode, &h_nd);
		path_put(&h_nd.path);
	}

	if (!err) {
		struct path tmp = *path;
		int did;

		vfsub_update_h_iattr(&tmp, &did);
		if (did) {
			tmp.dentry = path->dentry->d_parent;
			vfsub_update_h_iattr(&tmp, /*did*/NULL);
		}
		/*ignore*/
	}

out:
	return err;
}

int vfsub_symlink(struct inode *dir, struct path *path, const char *symname)
{
	int err;
	struct dentry *d;

	IMustLock(dir);

	d = path->dentry;
	path->dentry = d->d_parent;
	err = security_path_symlink(path, d, symname);
	path->dentry = d;
	if (unlikely(err))
		goto out;

	err = vfs_symlink(dir, path->dentry, symname);
	if (!err) {
		struct path tmp = *path;
		int did;

		vfsub_update_h_iattr(&tmp, &did);
		if (did) {
			tmp.dentry = path->dentry->d_parent;
			vfsub_update_h_iattr(&tmp, /*did*/NULL);
		}
		/*ignore*/
	}

out:
	return err;
}

int vfsub_mknod(struct inode *dir, struct path *path, int mode, dev_t dev)
{
	int err;
	struct dentry *d;

	IMustLock(dir);

	d = path->dentry;
	path->dentry = d->d_parent;
	err = security_path_mknod(path, d, mode, new_encode_dev(dev));
	path->dentry = d;
	if (unlikely(err))
		goto out;

	err = vfs_mknod(dir, path->dentry, mode, dev);
	if (!err) {
		struct path tmp = *path;
		int did;

		vfsub_update_h_iattr(&tmp, &did);
		if (did) {
			tmp.dentry = path->dentry->d_parent;
			vfsub_update_h_iattr(&tmp, /*did*/NULL);
		}
		/*ignore*/
	}

out:
	return err;
}

static int au_test_nlink(struct inode *inode)
{
	const unsigned int link_max = UINT_MAX >> 1; /* rough margin */

	if (!au_test_fs_no_limit_nlink(inode->i_sb)
	    || inode->i_nlink < link_max)
		return 0;
	return -EMLINK;
}

int vfsub_link(struct dentry *src_dentry, struct inode *dir, struct path *path)
{
	int err;
	struct dentry *d;

	IMustLock(dir);

	err = au_test_nlink(src_dentry->d_inode);
	if (unlikely(err))
		return err;

	d = path->dentry;
	path->dentry = d->d_parent;
	err = security_path_link(src_dentry, path, d);
	path->dentry = d;
	if (unlikely(err))
		goto out;

	lockdep_off();
	err = vfs_link(src_dentry, dir, path->dentry);
	lockdep_on();
	if (!err) {
		struct path tmp = *path;
		int did;

		/* fuse has different memory inode for the same inumber */
		vfsub_update_h_iattr(&tmp, &did);
		if (did) {
			tmp.dentry = path->dentry->d_parent;
			vfsub_update_h_iattr(&tmp, /*did*/NULL);
			tmp.dentry = src_dentry;
			vfsub_update_h_iattr(&tmp, /*did*/NULL);
		}
		/*ignore*/
	}

out:
	return err;
}

int vfsub_rename(struct inode *src_dir, struct dentry *src_dentry,
		 struct inode *dir, struct path *path)
{
	int err;
	struct path tmp = {
		.mnt	= path->mnt
	};
	struct dentry *d;

	IMustLock(dir);
	IMustLock(src_dir);

	d = path->dentry;
	path->dentry = d->d_parent;
	tmp.dentry = src_dentry->d_parent;
	err = security_path_rename(&tmp, src_dentry, path, d);
	path->dentry = d;
	if (unlikely(err))
		goto out;

	lockdep_off();
	err = vfs_rename(src_dir, src_dentry, dir, path->dentry);
	lockdep_on();
	if (!err) {
		int did;

		tmp.dentry = d->d_parent;
		vfsub_update_h_iattr(&tmp, &did);
		if (did) {
			tmp.dentry = src_dentry;
			vfsub_update_h_iattr(&tmp, /*did*/NULL);
			tmp.dentry = src_dentry->d_parent;
			vfsub_update_h_iattr(&tmp, /*did*/NULL);
		}
		/*ignore*/
	}

out:
	return err;
}

int vfsub_mkdir(struct inode *dir, struct path *path, int mode)
{
	int err;
	struct dentry *d;

	IMustLock(dir);

	d = path->dentry;
	path->dentry = d->d_parent;
	err = security_path_mkdir(path, d, mode);
	path->dentry = d;
	if (unlikely(err))
		goto out;

	err = vfs_mkdir(dir, path->dentry, mode);
	if (!err) {
		struct path tmp = *path;
		int did;

		vfsub_update_h_iattr(&tmp, &did);
		if (did) {
			tmp.dentry = path->dentry->d_parent;
			vfsub_update_h_iattr(&tmp, /*did*/NULL);
		}
		/*ignore*/
	}

out:
	return err;
}

int vfsub_rmdir(struct inode *dir, struct path *path)
{
	int err;
	struct dentry *d;

	IMustLock(dir);

	d = path->dentry;
	path->dentry = d->d_parent;
	err = security_path_rmdir(path, d);
	path->dentry = d;
	if (unlikely(err))
		goto out;

	lockdep_off();
	err = vfs_rmdir(dir, path->dentry);
	lockdep_on();
	if (!err) {
		struct path tmp = {
			.dentry	= path->dentry->d_parent,
			.mnt	= path->mnt
		};

		vfsub_update_h_iattr(&tmp, /*did*/NULL); /*ignore*/
	}

out:
	return err;
}

/* ---------------------------------------------------------------------- */

/* todo: support mmap_sem? */
ssize_t vfsub_read_u(struct file *file, char __user *ubuf, size_t count,
		     loff_t *ppos)
{
	ssize_t err;

	lockdep_off();
	err = vfs_read(file, ubuf, count, ppos);
	lockdep_on();
	if (err >= 0)
		vfsub_update_h_iattr(&file->f_path, /*did*/NULL); /*ignore*/
	return err;
}

/* todo: kernel_read()? */
ssize_t vfsub_read_k(struct file *file, void *kbuf, size_t count,
		     loff_t *ppos)
{
	ssize_t err;
	mm_segment_t oldfs;
	union {
		void *k;
		char __user *u;
	} buf;

	buf.k = kbuf;
	oldfs = get_fs();
	set_fs(KERNEL_DS);
	err = vfsub_read_u(file, buf.u, count, ppos);
	set_fs(oldfs);
	return err;
}

ssize_t vfsub_write_u(struct file *file, const char __user *ubuf, size_t count,
		      loff_t *ppos)
{
	ssize_t err;

	lockdep_off();
	err = vfs_write(file, ubuf, count, ppos);
	lockdep_on();
	if (err >= 0)
		vfsub_update_h_iattr(&file->f_path, /*did*/NULL); /*ignore*/
	return err;
}

ssize_t vfsub_write_k(struct file *file, void *kbuf, size_t count, loff_t *ppos)
{
	ssize_t err;
	mm_segment_t oldfs;
	union {
		void *k;
		const char __user *u;
	} buf;

	buf.k = kbuf;
	oldfs = get_fs();
	set_fs(KERNEL_DS);
	err = vfsub_write_u(file, buf.u, count, ppos);
	set_fs(oldfs);
	return err;
}

int vfsub_flush(struct file *file, fl_owner_t id)
{
	int err;

	err = 0;
	if (file->f_op && file->f_op->flush) {
		if (!au_test_nfs(file->f_dentry->d_sb))
			err = file->f_op->flush(file, id);
		else {
			lockdep_off();
			err = file->f_op->flush(file, id);
			lockdep_on();
		}
		if (!err)
			vfsub_update_h_iattr(&file->f_path, /*did*/NULL);
		/*ignore*/
	}
	return err;
}

int vfsub_readdir(struct file *file, filldir_t filldir, void *arg)
{
	int err;

	lockdep_off();
	err = vfs_readdir(file, filldir, arg);
	lockdep_on();
	if (err >= 0)
		vfsub_update_h_iattr(&file->f_path, /*did*/NULL); /*ignore*/
	return err;
}

long vfsub_splice_to(struct file *in, loff_t *ppos,
		     struct pipe_inode_info *pipe, size_t len,
		     unsigned int flags)
{
	long err;

	lockdep_off();
	err = do_splice_to(in, ppos, pipe, len, flags);
	lockdep_on();
	file_accessed(in);
	if (err >= 0)
		vfsub_update_h_iattr(&in->f_path, /*did*/NULL); /*ignore*/
	return err;
}

long vfsub_splice_from(struct pipe_inode_info *pipe, struct file *out,
		       loff_t *ppos, size_t len, unsigned int flags)
{
	long err;

	lockdep_off();
	err = do_splice_from(pipe, out, ppos, len, flags);
	lockdep_on();
	if (err >= 0)
		vfsub_update_h_iattr(&out->f_path, /*did*/NULL); /*ignore*/
	return err;
}

int vfsub_fsync(struct file *file, struct path *path, int datasync)
{
	int err;

	/* file can be NULL */
	lockdep_off();
	err = vfs_fsync(file, datasync);
	lockdep_on();
	if (!err) {
		if (!path) {
			AuDebugOn(!file);
			path = &file->f_path;
		}
		vfsub_update_h_iattr(path, /*did*/NULL); /*ignore*/
	}
	return err;
}

/* cf. open.c:do_sys_truncate() and do_sys_ftruncate() */
int vfsub_trunc(struct path *h_path, loff_t length, unsigned int attr,
		struct file *h_file)
{
	int err;
	struct inode *h_inode;

	h_inode = h_path->dentry->d_inode;
	if (!h_file) {
		err = mnt_want_write(h_path->mnt);
		if (err)
			goto out;
		err = inode_permission(h_inode, MAY_WRITE);
		if (err)
			goto out_mnt;
		err = get_write_access(h_inode);
		if (err)
			goto out_mnt;
		err = break_lease(h_inode, O_WRONLY);
		if (err)
			goto out_inode;
	}

	err = locks_verify_truncate(h_inode, h_file, length);
	if (!err)
		err = security_path_truncate(h_path);
	if (!err) {
		lockdep_off();
		err = do_truncate(h_path->dentry, length, attr, h_file);
		lockdep_on();
	}

out_inode:
	if (!h_file)
		put_write_access(h_inode);
out_mnt:
	if (!h_file)
		mnt_drop_write(h_path->mnt);
out:
	return err;
}

/* ---------------------------------------------------------------------- */

struct au_vfsub_mkdir_args {
	int *errp;
	struct inode *dir;
	struct path *path;
	int mode;
};

static void au_call_vfsub_mkdir(void *args)
{
	struct au_vfsub_mkdir_args *a = args;
	*a->errp = vfsub_mkdir(a->dir, a->path, a->mode);
}

int vfsub_sio_mkdir(struct inode *dir, struct path *path, int mode)
{
	int err, do_sio, wkq_err;

	do_sio = au_test_h_perm_sio(dir, MAY_EXEC | MAY_WRITE);
	if (!do_sio)
		err = vfsub_mkdir(dir, path, mode);
	else {
		struct au_vfsub_mkdir_args args = {
			.errp	= &err,
			.dir	= dir,
			.path	= path,
			.mode	= mode
		};
		wkq_err = au_wkq_wait(au_call_vfsub_mkdir, &args);
		if (unlikely(wkq_err))
			err = wkq_err;
	}

	return err;
}

struct au_vfsub_rmdir_args {
	int *errp;
	struct inode *dir;
	struct path *path;
};

static void au_call_vfsub_rmdir(void *args)
{
	struct au_vfsub_rmdir_args *a = args;
	*a->errp = vfsub_rmdir(a->dir, a->path);
}

int vfsub_sio_rmdir(struct inode *dir, struct path *path)
{
	int err, do_sio, wkq_err;

	do_sio = au_test_h_perm_sio(dir, MAY_EXEC | MAY_WRITE);
	if (!do_sio)
		err = vfsub_rmdir(dir, path);
	else {
		struct au_vfsub_rmdir_args args = {
			.errp	= &err,
			.dir	= dir,
			.path	= path
		};
		wkq_err = au_wkq_wait(au_call_vfsub_rmdir, &args);
		if (unlikely(wkq_err))
			err = wkq_err;
	}

	return err;
}

/* ---------------------------------------------------------------------- */

struct notify_change_args {
	int *errp;
	struct path *path;
	struct iattr *ia;
};

static void call_notify_change(void *args)
{
	struct notify_change_args *a = args;
	struct inode *h_inode;

	h_inode = a->path->dentry->d_inode;
	IMustLock(h_inode);

	*a->errp = -EPERM;
	if (!IS_IMMUTABLE(h_inode) && !IS_APPEND(h_inode)) {
		*a->errp = notify_change(a->path->dentry, a->ia);
		if (!*a->errp)
			vfsub_update_h_iattr(a->path, /*did*/NULL); /*ignore*/
	}
	AuTraceErr(*a->errp);
}

int vfsub_notify_change(struct path *path, struct iattr *ia)
{
	int err;
	struct notify_change_args args = {
		.errp	= &err,
		.path	= path,
		.ia	= ia
	};

	call_notify_change(&args);

	return err;
}

int vfsub_sio_notify_change(struct path *path, struct iattr *ia)
{
	int err, wkq_err;
	struct notify_change_args args = {
		.errp	= &err,
		.path	= path,
		.ia	= ia
	};

	wkq_err = au_wkq_wait(call_notify_change, &args);
	if (unlikely(wkq_err))
		err = wkq_err;

	return err;
}

/* ---------------------------------------------------------------------- */

struct unlink_args {
	int *errp;
	struct inode *dir;
	struct path *path;
};

static void call_unlink(void *args)
{
	struct unlink_args *a = args;
	struct dentry *d = a->path->dentry;
	struct inode *h_inode;
	const int stop_sillyrename = (au_test_nfs(d->d_sb)
				      && d->d_count == 1);

	IMustLock(a->dir);

	a->path->dentry = d->d_parent;
	*a->errp = security_path_unlink(a->path, d);
	a->path->dentry = d;
	if (unlikely(*a->errp))
		return;

	if (!stop_sillyrename)
		dget(d);
	h_inode = d->d_inode;
	if (h_inode)
		ihold(h_inode);

	lockdep_off();
	*a->errp = vfs_unlink(a->dir, d);
	lockdep_on();
	if (!*a->errp) {
		struct path tmp = {
			.dentry = d->d_parent,
			.mnt	= a->path->mnt
		};
		vfsub_update_h_iattr(&tmp, /*did*/NULL); /*ignore*/
	}

	if (!stop_sillyrename)
		dput(d);
	if (h_inode)
		iput(h_inode);

	AuTraceErr(*a->errp);
}

/*
 * @dir: must be locked.
 * @dentry: target dentry.
 */
int vfsub_unlink(struct inode *dir, struct path *path, int force)
{
	int err;
	struct unlink_args args = {
		.errp	= &err,
		.dir	= dir,
		.path	= path
	};

	if (!force)
		call_unlink(&args);
	else {
		int wkq_err;

		wkq_err = au_wkq_wait(call_unlink, &args);
		if (unlikely(wkq_err))
			err = wkq_err;
	}

	return err;
}
